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

/* ===== US-010: End-to-end pipeline integration ===== */

/* Test: jacl_run exists and executes simple program */
static int test_jacl_run_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print 42]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: motivating example [print [+ 1 2]] outputs "3\n" */
static int test_motivating_example(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [+ 1 2]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: parse errors are reported, execution does not proceed */
static int test_parse_error_reported(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  /* Unclosed bracket is a parse error */
  VMResult result = jacl_run("[+ 1", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "parse") != NULL);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: compile errors are reported, execution does not proceed */
static int test_compile_error_reported(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  /* Long variable name > 128 bytes is a compile error */
  VMResult result = jacl_run(
      "def aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhh"
      "iiiiiiiijjjjjjjjkkkkkkkkllllllllmmmmmmmmnnnnnnnnooooooooppppppppq 42",
      &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "128-byte") != NULL);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: runtime errors are reported with line numbers */
static int test_runtime_error_with_line(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  /* Type mismatch on line 2 */
  VMResult result = jacl_run("x = $true\n[+ $x 1]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(vm.error_line == 2);
  ASSERT(strstr(vm.error_message, "bool") != NULL);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: program with deliberate errors verifies error reporting */
static int test_deliberate_errors(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  /* String >7 bytes now compiles as heap-interned string (no error) */
  VMResult result = jacl_run("\"abcdefgh\"", &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);

  /* Undefined variable */
  vm_init(&vm, &arena);
  result = jacl_run("[print $nope]", &vm, &arena);
  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "nope") != NULL);

  /* Division by zero produces error-flagged value (not runtime error) */
  PrintCapture cap = { .len = 0 };
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  result = jacl_run("[print [/ 1 0]]", &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "<error: 0>\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: 10+ line program mixing def, arithmetic, comparisons, print, nested subcommands */
static int test_comprehensive_program(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  const char* program =
    "x = 10\n"
    "y = 20\n"
    "sum = [+ $x $y]\n"
    "[print $sum]\n"
    "[print [* $x 2]]\n"
    "gt = [> $x 5]\n"
    "[print $gt]\n"
    "[print [== $x 10]]\n"
    "[print [< $y 100]]\n"
    "z = [+ [* $x $y] 5]\n"
    "[print $z]\n"
    "[print [- $z $sum]]";

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf,
    "30\n"
    "20\n"
    "true\n"
    "true\n"
    "true\n"
    "205\n"
    "175\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: f32 arithmetic through the full pipeline */
static int test_f32_pipeline(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [+ 1.5 2.5]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "4\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multi-statement with semicolons via jacl_run */
static int test_semicolons_pipeline(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("a = 3\nb = 7\n[print [+ $a $b]]",
                             &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: empty program via jacl_run succeeds */
static int test_empty_program(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  VMResult result = jacl_run("", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-003 (M6): Comma as command separator ===== */

static int test_comma_separator_toplevel(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("def x 1, def y 2, [print [+ $x $y]]",
                             &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_comma_separator_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("{ def a 10, def b 20, [print [+ $a $b]] }",
                             &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-005 (M6): $var lines as command calls ===== */

static int test_var_ref_command_call(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* proc greet {} { 42 } defines a proc that returns 42.
     def f $greet binds f to the greet closure (via $greet var ref).
     [$f] calls f (zero-arg), which calls greet, returning 42. */
  VMResult result = jacl_run(
    "proc greet {} { 42 }\n"
    "def f $greet\n"
    "[print [$f]]",
    &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- US-005: Context lifecycle and recursive vm_exec --- */

/* Test: jacl_ctx_new creates a working root context, child context shares
 * parent intern table but has independent VM state. */
static int test_context_lifecycle(void) {
  jacl_context_t *root = jacl_ctx_new(NULL);
  ASSERT(root != NULL);
  ASSERT(root->parent == NULL);
  ASSERT(root->owns_intern_table == true);
  ASSERT(root->restriction_set == UINT64_MAX);

  jacl_context_t *child = jacl_ctx_new(root);
  ASSERT(child != NULL);
  ASSERT(child->parent == root);
  ASSERT(child->owns_intern_table == false);
  /* Child shares parent's intern table */
  ASSERT(child->vm.intern_table == &root->intern_table);

  jacl_ctx_destroy(child);
  jacl_ctx_destroy(root);
  TEST_PASS();
}

/* Test: two independent vm_exec invocations via contexts complete without
 * state corruption. This proves the macro expansion state (previously
 * file-static) is safely per-context. */
static int test_recursive_vm_exec(void) {
  /* First invocation: run code with a macro in context A */
  jacl_context_t *ctx_a = jacl_ctx_new(NULL);
  ASSERT(ctx_a != NULL);

  PrintCapture cap_a = { .len = 0 };
  ctx_a->vm.print_fn = capture_print;
  ctx_a->vm.print_ctx = &cap_a;

  ExpandState es_a;
  memset(&es_a, 0, sizeof(es_a));

  LexResult tok_a = lexer_lex(
      "defmacro double {x} { syntax-quote [+ ~x ~x] }\n"
      "[print [double 21]]",
      &ctx_a->arena);
  ParseResult parse_a = parser_parse(tok_a, &ctx_a->arena);
  ASSERT(parse_a.error_count == 0);

  CompileResult cr_a = compiler_compile(parse_a, &ctx_a->arena,
      &ctx_a->intern_table, &ctx_a->vm.heap, NULL, &es_a, JACL_NIL);
  ASSERT(cr_a.error_count == 0);

  ctx_a->vm.struct_registry = cr_a.struct_registry;
  VMResult r_a = vm_exec(&ctx_a->vm, &cr_a.chunk);
  ASSERT(r_a == VM_OK);
  ASSERT_STR_EQ(cap_a.buf, "42\n");

  /* Second invocation: independently run code with a different macro in
   * context B. If the old file-statics leaked, expansion state from A
   * would corrupt B. */
  jacl_context_t *ctx_b = jacl_ctx_new(NULL);
  ASSERT(ctx_b != NULL);

  PrintCapture cap_b = { .len = 0 };
  ctx_b->vm.print_fn = capture_print;
  ctx_b->vm.print_ctx = &cap_b;

  ExpandState es_b;
  memset(&es_b, 0, sizeof(es_b));

  LexResult tok_b = lexer_lex(
      "defmacro triple {x} { syntax-quote [* ~x 3] }\n"
      "[print [triple 10]]",
      &ctx_b->arena);
  ParseResult parse_b = parser_parse(tok_b, &ctx_b->arena);
  ASSERT(parse_b.error_count == 0);

  CompileResult cr_b = compiler_compile(parse_b, &ctx_b->arena,
      &ctx_b->intern_table, &ctx_b->vm.heap, NULL, &es_b, JACL_NIL);
  ASSERT(cr_b.error_count == 0);

  ctx_b->vm.struct_registry = cr_b.struct_registry;
  VMResult r_b = vm_exec(&ctx_b->vm, &cr_b.chunk);
  ASSERT(r_b == VM_OK);
  ASSERT_STR_EQ(cap_b.buf, "30\n");

  /* Verify A's output is unchanged — no cross-contamination */
  ASSERT_STR_EQ(cap_a.buf, "42\n");

  jacl_ctx_destroy(ctx_b);
  jacl_ctx_destroy(ctx_a);
  TEST_PASS();
}

/* ===== US-006 (macro-eval): jacl_ctx_run API ===== */

/* Test: jacl_ctx_run_source with valid source returns correct value */
static int test_ctx_run_source_valid(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src = "[print 42]\n[+ 1 2]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "42\n");
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 3);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: jacl_ctx_run_source with parse error populates err_out */
static int test_ctx_run_source_parse_error(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[+ 1";  /* unclosed bracket */
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_COMPILE);
    ASSERT(err.message != NULL);
    ASSERT(jacl_is_nil(result));

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: jacl_ctx_run_source with runtime error populates err_out */
static int test_ctx_run_source_runtime_error(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[undefined_fn 1]";  /* undefined variable -> runtime error */
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_RUNTIME);
    ASSERT(err.message != NULL);
    ASSERT(jacl_is_nil(result));

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: nested jacl_ctx_run_source calls (reentrancy) */
static int test_ctx_run_source_nested(void) {
    /* Outer context runs code with a macro */
    jacl_context_t *outer = jacl_ctx_new(NULL);
    ASSERT(outer != NULL);

    PrintCapture cap_outer = { .len = 0 };
    outer->vm.print_fn = capture_print;
    outer->vm.print_ctx = &cap_outer;

    const char *src_outer = "defmacro double {x} { syntax-quote [+ ~x ~x] }\n"
                            "[print [double 10]]";
    JaclError err_outer;
    JaclVal r_outer = jacl_ctx_run_source(outer, src_outer, strlen(src_outer),
                                           UINT64_MAX, &err_outer);
    ASSERT(err_outer.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap_outer.buf, "20\n");

    /* Inner context runs independent code — no cross-contamination */
    jacl_context_t *inner = jacl_ctx_new(NULL);
    ASSERT(inner != NULL);

    PrintCapture cap_inner = { .len = 0 };
    inner->vm.print_fn = capture_print;
    inner->vm.print_ctx = &cap_inner;

    const char *src_inner = "[print [* 5 7]]";
    JaclError err_inner;
    JaclVal r_inner = jacl_ctx_run_source(inner, src_inner, strlen(src_inner),
                                           UINT64_MAX, &err_inner);
    ASSERT(err_inner.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap_inner.buf, "35\n");

    /* Verify outer's output unchanged */
    ASSERT_STR_EQ(cap_outer.buf, "20\n");

    jacl_ctx_destroy(inner);
    jacl_ctx_destroy(outer);
    TEST_PASS();
}

/* ===== M14 (interpret): [interpret $src] builtin — Phase 1 ===== */

/* Test: [interpret "..."] returns scalar result on parent heap */
static int test_interpret_basic(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret \"[+ 1 2]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 3);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret] returning a string copies it onto parent heap */
static int test_interpret_string_result(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* [concat "hello " "world!"] → "hello world!" (long enough to exceed
       the 7-byte inline-string path, exercising heap-string materialization) */
    const char *src =
        "[interpret \"[concat \\\"hello \\\" \\\"world!\\\"]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_string(result));

    char buf[64] = {0};
    uint32_t n = jacl_string_data(result, buf, sizeof(buf));
    ASSERT(n == strlen("hello world!"));
    ASSERT_STR_EQ(buf, "hello world!");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret] on source with a parse error returns an error value */
static int test_interpret_parse_error(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret \"[+ 1\"]";  /* unclosed bracket */
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);  /* outer run succeeds */
    ASSERT(jacl_is_error(result));         /* inner parse error → error value */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret] on source with a runtime error returns an error value */
static int test_interpret_runtime_error(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret \"[undefined_fn 1]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_error(result));

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret] inherits parent's print function */
static int test_interpret_with_print(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src = "[interpret \"[print 99]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "99\n");
    (void)result;  /* return value of [print ...] is nil */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret 42] on non-string argument returns error value (not exception) */
static int test_interpret_type_error(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret 42]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);  /* outer call succeeds */
    ASSERT(jacl_is_error(result));         /* result is error value */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret 42 "src"] with non-map prelude returns error value */
static int test_interpret_prelude_type_error(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret 42 \"[+ 1 2]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);  /* outer call succeeds */
    ASSERT(jacl_is_error(result));         /* result is error value */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: result from [interpret] is usable in subsequent parent operations */
static int test_interpret_shared_heap(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src = "[print [+ [interpret \"[* 3 4]\"] 1]]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "13\n");
    (void)result;

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret] returning a vector works (no more Phase 1 complex-value error) */
static int test_interpret_vector_result(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret \"[vec 1 2 3]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_vector(result));
    ASSERT(jacl_vec_count((jacl_vec_root*)jacl_as_ptr(result)) == 3);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret] returning a map works (no more Phase 1 complex-value error) */
static int test_interpret_map_result(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret \"[map \\\"a\\\" 1 \\\"b\\\" 2]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_map(result));
    ASSERT(jacl_map_count((jacl_map_node*)jacl_as_ptr(result)) == 2);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== source_to_closure_in_place tests ===== */

/* Test: valid source compiles to a closure, calling it produces expected value */
static int test_source_to_closure_valid(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src = "[+ 1 2]";
    JaclError err;
    ExpandState es;
    memset(&es, 0, sizeof(es));
    es.ctx = ctx;

    JaclInternTable *itab = &ctx->intern_table;

    JaclVal closure_val = source_to_closure_in_place(
        src, strlen(src), &ctx->arena, &ctx->vm.heap,
        itab, &es, &err, JACL_NIL);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_closure(closure_val));

    /* Execute the closure's chunk via vm_exec (top-level compiled code) */
    JaclClosure *cl = jacl_as_closure(closure_val);
    ctx->vm.intern_table = itab;
    VMResult r = vm_exec(&ctx->vm, &cl->chunk);

    ASSERT(r == VM_OK);
    ASSERT(ctx->vm.stack_top > 0);
    JaclVal result = ctx->vm.stack[0];
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 3);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: parse error returns JACL_ERROR_COMPILE with message */
static int test_source_to_closure_parse_error(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[+ 1";  /* unclosed bracket */
    JaclError err;
    ExpandState es;
    memset(&es, 0, sizeof(es));
    es.ctx = ctx;

    JaclVal closure_val = source_to_closure_in_place(
        src, strlen(src), &ctx->arena, &ctx->vm.heap,
        &ctx->intern_table, &es, &err, JACL_NIL);

    ASSERT(jacl_is_nil(closure_val));
    ASSERT(err.kind == JACL_ERROR_COMPILE);
    ASSERT(err.message != NULL);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: compile error (bad builtin arity) returns JACL_ERROR_COMPILE with message */
static int test_source_to_closure_compile_error(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[def]";  /* def with no args = compile error */
    JaclError err;
    ExpandState es;
    memset(&es, 0, sizeof(es));
    es.ctx = ctx;

    JaclVal closure_val = source_to_closure_in_place(
        src, strlen(src), &ctx->arena, &ctx->vm.heap,
        &ctx->intern_table, &es, &err, JACL_NIL);

    ASSERT(jacl_is_nil(closure_val));
    ASSERT(err.kind == JACL_ERROR_COMPILE);
    ASSERT(err.message != NULL);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== US-003: Compiler prelude map as compile-time env seed ===== */

/* Test: compiling [log 42] with prelude map {"log": $my-log} succeeds;
 * closure's log reference resolves to $my-log */
static int test_prelude_compile_success(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Build a prelude map: {"log": <a closure that returns its arg + 100>} */
    /* First, compile a closure for the "log" function */
    const char *log_src = "proc my-log {x} { + $x 100 }; $my-log";
    JaclError log_err;
    JaclVal log_result = jacl_ctx_run_source(ctx, log_src, strlen(log_src),
                                              UINT64_MAX, &log_err);
    ASSERT(log_err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_closure(log_result));

    /* Build the prelude map: {"log": $my-log} */
    gc__current_heap = &ctx->vm.heap;
    JaclVal key_log = jacl_intern(&ctx->vm.heap, &ctx->intern_table, "log", 3);
    jacl_map_node *pmap = jacl_map_set(NULL, key_log, log_result);
    JaclVal prelude = jacl_map_ptr(pmap);

    /* Compile "[log 42]" with the prelude map */
    const char *src = "[log 42]";
    JaclError err;
    ExpandState es;
    memset(&es, 0, sizeof(es));
    es.ctx = ctx;

    JaclVal closure_val = source_to_closure_in_place(
        src, strlen(src), &ctx->arena, &ctx->vm.heap,
        &ctx->intern_table, &es, &err, prelude);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_closure(closure_val));

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: compiling [log 42] with empty prelude map {} returns compile error
 * for unresolved log */
static int test_prelude_compile_error_unresolved(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Empty prelude map (no entries) → log should be unresolved */
    JaclVal prelude = jacl_map_ptr(NULL);  /* empty map */

    const char *src = "[log 42]";
    JaclError err;
    ExpandState es;
    memset(&es, 0, sizeof(es));
    es.ctx = ctx;

    JaclVal closure_val = source_to_closure_in_place(
        src, strlen(src), &ctx->arena, &ctx->vm.heap,
        &ctx->intern_table, &es, &err, prelude);

    ASSERT(jacl_is_nil(closure_val));
    ASSERT(err.kind == JACL_ERROR_COMPILE);
    ASSERT(err.message != NULL);
    /* Error message should mention 'log' */
    ASSERT(strstr(err.message, "log") != NULL);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: prelude map entries are reachable via normal env lookup during
 * interpreted execution — compile with prelude, populate VM env, execute */
static int test_prelude_entries_reachable(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Build a closure that returns its arg + 100 */
    const char *log_src = "proc my-log {x} { + $x 100 }; $my-log";
    JaclError log_err;
    JaclVal log_result = jacl_ctx_run_source(ctx, log_src, strlen(log_src),
                                              UINT64_MAX, &log_err);
    ASSERT(log_err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_closure(log_result));

    /* Build the prelude map: {"log": $my-log} */
    gc__current_heap = &ctx->vm.heap;
    JaclVal key_log = jacl_intern(&ctx->vm.heap, &ctx->intern_table, "log", 3);
    jacl_map_node *pmap = jacl_map_set(NULL, key_log, log_result);
    JaclVal prelude = jacl_map_ptr(pmap);

    /* Compile "[log 42]" with the prelude map */
    const char *src = "[log 42]";
    JaclError err;
    ExpandState es;
    memset(&es, 0, sizeof(es));
    es.ctx = ctx;

    JaclVal closure_val = source_to_closure_in_place(
        src, strlen(src), &ctx->arena, &ctx->vm.heap,
        &ctx->intern_table, &es, &err, prelude);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_closure(closure_val));

    /* Populate the VM env with prelude entries so OP_GET_GLOBAL finds them.
     * The compiler emits inline strings for names ≤7 bytes, so the env key
     * must match (vm__env_get uses identity comparison). */
    JaclVal env_key_log = jacl_inline_string("log", 3);
    vm__env_set(&ctx->vm, env_key_log, log_result);

    /* Execute the compiled closure */
    JaclClosure *cl = jacl_as_closure(closure_val);
    ctx->vm.intern_table = &ctx->intern_table;
    VMResult r = vm_exec(&ctx->vm, &cl->chunk);

    ASSERT(r == VM_OK);
    ASSERT(ctx->vm.stack_top > 0);
    JaclVal result = ctx->vm.stack[0];
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 142);  /* 42 + 100 */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== US-004: [interpret-prelude] returns default permissive prelude ===== */

/* Test: [interpret-prelude] returns a map with interpret as a key */
static int test_interpret_prelude_has_interpret(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret-prelude]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_map(result));

    /* Check that "interpret" is a key in the map.
       "interpret" is 9 bytes (>7) so use jacl_intern to match VM handler. */
    jacl_map_node *m = (jacl_map_node *)jacl_as_ptr(result);
    JaclVal key_interpret = jacl_intern(&ctx->vm.heap, &ctx->intern_table,
                                         "interpret", 9);
    JaclVal val = jacl_map_get(m, key_interpret);
    ASSERT(!jacl_is_nil(val));

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: returned map does NOT contain core names like +, if, def */
static int test_interpret_prelude_no_core(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret-prelude]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_map(result));

    jacl_map_node *m = (jacl_map_node *)jacl_as_ptr(result);

    /* Core builtins must NOT be in the map */
    JaclVal key_plus = jacl_inline_string("+", 1);
    ASSERT(jacl_is_nil(jacl_map_get(m, key_plus)));

    JaclVal key_if = jacl_inline_string("if", 2);
    ASSERT(jacl_is_nil(jacl_map_get(m, key_if)));

    JaclVal key_def = jacl_inline_string("def", 3);
    ASSERT(jacl_is_nil(jacl_map_get(m, key_def)));

    JaclVal key_vec = jacl_inline_string("vec", 3);
    ASSERT(jacl_is_nil(jacl_map_get(m, key_vec)));

    JaclVal key_map = jacl_inline_string("map", 3);
    ASSERT(jacl_is_nil(jacl_map_get(m, key_map)));

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [map-has [interpret-prelude] "print"] is true (via source execution) */
static int test_interpret_prelude_has_print(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[map-has [interpret-prelude] \"print\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(result == JACL_TRUE);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret-prelude] values are native-fn refs (not just $true sentinels) */
static int test_interpret_prelude_native_fn_values(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret-prelude]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_map(result));

    jacl_map_node *m = (jacl_map_node *)jacl_as_ptr(result);

    /* Check that "print" value is a native-fn (not $true) */
    JaclVal key_print = jacl_inline_string("print", 5);
    JaclVal val_print = jacl_map_get(m, key_print);
    ASSERT(!jacl_is_nil(val_print));
    ASSERT(jacl_is_native_fn(val_print));  /* NEW: values are native fn refs */

    /* Check that "interpret" value is also a native-fn */
    JaclVal key_interpret = jacl_intern(&ctx->vm.heap, &ctx->intern_table,
                                         "interpret", 9);
    JaclVal val_interpret = jacl_map_get(m, key_interpret);
    ASSERT(!jacl_is_nil(val_interpret));
    ASSERT(jacl_is_native_fn(val_interpret));

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret [interpret-prelude] "[print 42]"] works with direct opcode emission.
 * The native fn ref for "print" triggers direct OP_PRINT, not env lookup. */
static int test_interpret_prelude_direct_opcode(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Using default prelude with native fn refs — print compiles to OP_PRINT */
    const char *src = "[interpret [interpret-prelude] \"[print 42]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    /* print returns nil */
    ASSERT(jacl_is_nil(result));

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [map-set [interpret-prelude] "print" $closure] overrides the native fn ref.
 * When a closure replaces a native fn ref, the closure is called (env lookup path). */
static int test_interpret_prelude_override_builtin(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Override print with a closure that returns x + 1000 instead of printing */
    const char *src =
        "proc my-print {x} { + $x 1000 };"
        "[interpret [map-set [interpret-prelude] \"print\" $my-print] \"[print 42]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    /* Custom closure returns 42 + 1000 = 1042 */
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 1042);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== US-005 (sandboxed eval): [interpret $prelude $src] two-arg form ===== */

/* Test: [interpret {"x": 42} "[+ $x 1]"] returns 43
 * Note: $x inside a JACL string triggers interpolation, so we build the
 * inner source via concat to avoid outer-scope resolution. */
static int test_interpret_two_arg_prelude_var(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Build inner source "[+ $x 1]" without triggering $x interpolation */
    const char *src =
        "def inner-src [concat \"[+ \" \"$\" \"x 1]\"];"
        "[interpret [map \"x\" 42] $inner-src]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 43);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret {} "[+ 1 2]"] returns 3 (core always available) */
static int test_interpret_two_arg_empty_prelude_core(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret [map] \"[+ 1 2]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 3);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret {} "[print \"hi\"]"] returns compile-error (print not in prelude) */
static int test_interpret_two_arg_blocked_print(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret [map] \"[print \\\"hi\\\"]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);  /* outer succeeds */
    ASSERT(jacl_is_error(result));          /* inner compile error */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret [map-remove [interpret-prelude] "interpret"] "[interpret \"[+ 1 2]\"]"]
 * returns compile-error (nested interpret blocked) */
static int test_interpret_two_arg_blocked_nested(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src =
        "[interpret [map-remove [interpret-prelude] \"interpret\"]"
        " \"[interpret \\\"[+ 1 2]\\\"]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);  /* outer succeeds */
    ASSERT(jacl_is_error(result));          /* inner compile error */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret [map-set [map] "log" $my-closure] "[log 42]"] calls the closure */
static int test_interpret_two_arg_custom_closure(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Define a closure that adds 100, then pass it via prelude as "log" */
    const char *src =
        "proc adder {x} { + $x 100 };"
        "[interpret [map-set [map] \"log\" $adder] \"[log 42]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 142);  /* 42 + 100 */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: closure in prelude sees parent's mutable state (shared heap/VM) */
static int test_interpret_two_arg_shared_state(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Create a box (mutable cell) in the parent, pass a reader closure
     * via prelude that reads the box, and verify the interpreted code
     * can observe the parent's state. */
    const char *src =
        "def b [box 10];"
        "proc read-b {} { deref $b };"
        "[interpret [map-set [map] \"read-b\" $read-b] \"[read-b]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 10);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== US-006 (sandboxed eval): Non-core builtin opcode-vs-env downgrade ===== */

/* Test: [interpret {"print": $my-print} "[print \"hi\"]"] calls $my-print
 * instead of the built-in OP_PRINT opcode.  Proves the opcode downgrade works. */
static int test_sandbox_print_downgrade(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Define a closure that returns its arg + 100, pass it as "print" in prelude.
     * If opcode downgrade works, [print 42] calls our closure → returns 142.
     * If NOT downgraded, OP_PRINT would print "42" and return nil. */
    const char *src =
        "proc my-print {x} { + $x 100 };"
        "[interpret [map-set [map] \"print\" $my-print] \"[print 42]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 142);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret {} "[spawn { 42 }]"] is a compile error (spawn not in prelude) */
static int test_sandbox_spawn_blocked(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret [map] \"[spawn { 42 }]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);  /* outer succeeds */
    ASSERT(jacl_is_error(result));          /* inner compile error: spawn not in prelude */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: [interpret {} "[+ 1 [* 2 3]]"] returns 7 (core arithmetic always works) */
static int test_sandbox_core_arithmetic(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    const char *src = "[interpret [map] \"[+ 1 [* 2 3]]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 7);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: macro defined inside interpreted code that references a non-core
 * builtin gets the sandbox treatment (macro-expanded output goes through
 * sandbox compile, so print routes through the prelude closure). */
static int test_sandbox_macro_noncore(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Inner source defines a macro that wraps [print ~x], then calls it.
     * Because "print" in the prelude maps to our custom closure ($my-fn),
     * the macro-expanded [print 42] should be downgraded to env lookup and
     * call $my-fn(42) → 42 + 200 = 242. */
    const char *src =
        "proc my-fn {x} { + $x 200 };"
        "def inner-src [concat"
        "  \"defmacro do-print {x} { syntax-quote [print ~x] };\""
        "  \" [do-print 42]\"];"
        "[interpret [map-set [interpret-prelude] \"print\" $my-fn] $inner-src]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 242);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== US-007 (macro-eval): Staged syntax-quote compilation ===== */

/* Helper: run source in a context, return the result */
static JaclVal staged_run(const char *src, JaclError *err) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, err);
    /* NOTE: result is on the context's heap; we return it but the caller
       must inspect it before destroying the context.  For these tests we
       leak the context intentionally — acceptable in test code. */
    (void)ctx;  /* intentional leak for test simplicity */
    return result;
}

/* Test: syntax-quote with literal substitution (no unquotes) */
static int test_staged_sq_literal(void) {
    JaclError err;
    JaclVal result = staged_run("syntax-quote [+ 1 2]", &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_syntax(result));

    JaclSyntax *syn = jacl_as_syntax(result);
    ASSERT(syn->kind == SYNTAX_COMMAND);

    /* Head should be a lit-string for "+" (bare words in commands parse as strings) */
    JaclSyntax *head = jacl_as_syntax(syn->data.command.head);
    ASSERT(head->kind == SYNTAX_LIT_STRING);

    /* Args should be [lit-int(1), lit-int(2)] */
    JaclVal args = syn->data.command.args;
    ASSERT(jacl_is_vector(args));
    ASSERT(jacl_vec_count((jacl_vec_root*)jacl_as_ptr(args)) == 2);

    jacl_vec_get_result g0 = jacl_vec_get((jacl_vec_root*)jacl_as_ptr(args), 0);
    ASSERT(g0.found);
    JaclSyntax *a0 = jacl_as_syntax(g0.value);
    ASSERT(a0->kind == SYNTAX_LIT_INT);
    ASSERT(a0->data.lit_int.value == 1);

    jacl_vec_get_result g1 = jacl_vec_get((jacl_vec_root*)jacl_as_ptr(args), 1);
    ASSERT(g1.found);
    JaclSyntax *a1 = jacl_as_syntax(g1.value);
    ASSERT(a1->kind == SYNTAX_LIT_INT);
    ASSERT(a1->data.lit_int.value == 2);

    TEST_PASS();
}

/* Test: syntax-quote with unquote (~) substitution */
static int test_staged_sq_unquote(void) {
    /* Wrap in a proc so 'x' is a local binding */
    const char *src =
        "proc make-it {x} { syntax-quote [+ ~x 10] }\n"
        "[make-it [make-syntax \"lit-int\" 42]]";
    JaclError err;
    JaclVal result = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_sq_unquote err: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_syntax(result));

    JaclSyntax *syn = jacl_as_syntax(result);
    ASSERT(syn->kind == SYNTAX_COMMAND);

    /* First arg should be the unquoted value: lit-int(42) */
    JaclVal args = syn->data.command.args;
    jacl_vec_get_result g0 = jacl_vec_get((jacl_vec_root*)jacl_as_ptr(args), 0);
    ASSERT(g0.found);
    JaclSyntax *a0 = jacl_as_syntax(g0.value);
    ASSERT(a0->kind == SYNTAX_LIT_INT);
    ASSERT(a0->data.lit_int.value == 42);

    /* Second arg should be literal 10 */
    jacl_vec_get_result g1 = jacl_vec_get((jacl_vec_root*)jacl_as_ptr(args), 1);
    ASSERT(g1.found);
    JaclSyntax *a1 = jacl_as_syntax(g1.value);
    ASSERT(a1->kind == SYNTAX_LIT_INT);
    ASSERT(a1->data.lit_int.value == 10);

    TEST_PASS();
}

/* Test: syntax-quote with unquote-splicing (~@) */
static int test_staged_sq_splice(void) {
    /* Create a vec of syntax objects and splice them into a command */
    const char *src =
        "proc make-it {items} { syntax-quote [+ ~@items] }\n"
        "xs = [vec [make-syntax \"lit-int\" 1] [make-syntax \"lit-int\" 2] [make-syntax \"lit-int\" 3]]\n"
        "[make-it $xs]";
    JaclError err;
    JaclVal result = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_sq_splice err: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_syntax(result));

    JaclSyntax *syn = jacl_as_syntax(result);
    ASSERT(syn->kind == SYNTAX_COMMAND);

    /* Args should be [1, 2, 3] after splicing */
    JaclVal args = syn->data.command.args;
    uint32_t count = jacl_vec_count((jacl_vec_root*)jacl_as_ptr(args));
    ASSERT(count == 3);

    for (uint32_t i = 0; i < 3; i++) {
        jacl_vec_get_result g = jacl_vec_get((jacl_vec_root*)jacl_as_ptr(args), i);
        ASSERT(g.found);
        JaclSyntax *a = jacl_as_syntax(g.value);
        ASSERT(a->kind == SYNTAX_LIT_INT);
        ASSERT(a->data.lit_int.value == (int32_t)(i + 1));
    }

    TEST_PASS();
}

/* Test: nested syntax-quote is treated as literal */
static int test_staged_sq_nested(void) {
    const char *src = "syntax-quote { syntax-quote [+ ~x 1] }";
    JaclError err;
    JaclVal result = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_sq_nested err: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_syntax(result));

    /* The outer syntax-quote wraps a block containing an inner syntax-quote.
     * The result should be a block (or the inner expression) containing
     * a SYNTAX_SYNTAX_QUOTE node. */
    JaclSyntax *syn = jacl_as_syntax(result);
    /* The block { ... } compiles as a SYNTAX_BLOCK */
    ASSERT(syn->kind == SYNTAX_BLOCK);

    /* The single command inside is the nested syntax-quote */
    JaclVal cmds = syn->data.block.commands;
    uint32_t cnt = jacl_vec_count((jacl_vec_root*)jacl_as_ptr(cmds));
    ASSERT(cnt == 1);

    jacl_vec_get_result g0 = jacl_vec_get((jacl_vec_root*)jacl_as_ptr(cmds), 0);
    ASSERT(g0.found);
    JaclSyntax *inner = jacl_as_syntax(g0.value);
    /* Nested syntax-quote is emitted as a constant, so it should be SYNTAX_SYNTAX_QUOTE */
    ASSERT(inner->kind == SYNTAX_SYNTAX_QUOTE);

    TEST_PASS();
}

/* ===== US-008 (macro-eval): Staged macro expansion ===== */

/* Test: staged macro end-to-end — same output as legacy path */
static int test_staged_macro_e2e(void) {
    /* Legacy path */
    jacl_context_t *ctx_old = jacl_ctx_new(NULL);
    PrintCapture cap_old = { .len = 0 };
    ctx_old->vm.print_fn = capture_print;
    ctx_old->vm.print_ctx = &cap_old;

    const char *src =
        "defmacro double {x} { syntax-quote [+ ~x ~x] }\n"
        "[print [double 21]]";

    JaclError err_old;
    jacl_ctx_run_source(ctx_old, src, strlen(src), UINT64_MAX, &err_old);
    ASSERT(err_old.kind == JACL_ERROR_NONE);

    /* Staged path */
    jacl_context_t *ctx_new = jacl_ctx_new(NULL);
    PrintCapture cap_new = { .len = 0 };
    ctx_new->vm.print_fn = capture_print;
    ctx_new->vm.print_ctx = &cap_new;

    JaclError err_new;
    jacl_ctx_run_source(ctx_new, src, strlen(src), UINT64_MAX, &err_new);
    if (err_new.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_macro_e2e: %s\n", err_new.message ? err_new.message : "(null)");
    }
    ASSERT(err_new.kind == JACL_ERROR_NONE);

    /* Outputs must match */
    ASSERT_STR_EQ(cap_old.buf, "42\n");
    ASSERT_STR_EQ(cap_new.buf, cap_old.buf);

    jacl_ctx_destroy(ctx_new);
    jacl_ctx_destroy(ctx_old);
    TEST_PASS();
}

/* Test: staged macro with block body — multiple expressions */
static int test_staged_macro_block(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src =
        "defmacro triple {x} { syntax-quote [* ~x 3] }\n"
        "[print [triple 7]]";

    JaclError err;
    jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_macro_block: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "21\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: forward-referenced staged macros (A calls B defined later) */
static int test_staged_macro_forward_ref(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    /* macro inc calls +, macro add-two calls inc (defined earlier, but
     * both are registered before expansion so forward ref within the
     * expansion pass works). */
    const char *src =
        "defmacro inc {x} { syntax-quote [+ ~x 1] }\n"
        "defmacro add-two {x} { syntax-quote [+ ~x 2] }\n"
        "[print [inc 10]]\n"
        "[print [add-two 10]]";

    JaclError err;
    jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_macro_fwd: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "11\n12\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== US-009: Mark-propagation hygiene on staged path ===== */

/* Test: macro-introduced local does not shadow caller's variable (hygiene).
 * Template's `mut tmp 1` carries macro mark; caller's `$tmp` via ~x has mark 0.
 * Result: 99 + 1 = 100 (not 1 + 1 = 2). */
static int test_staged_hygiene_local_shadow(void) {
    const char *src =
        "defmacro m {x} {\n"
        "  syntax-quote {\n"
        "    mut tmp 1\n"
        "    [+ ~x $tmp]\n"
        "  }\n"
        "}\n"
        "proc run {} {\n"
        "  mut tmp 99\n"
        "  m $tmp\n"
        "}\n"
        "run";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 100);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "  staged hygiene shadow: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 100);

    TEST_PASS();
}

/* Test: two nested macro expansions with same name don't collide.
 * add_ten binds t=10, add_hundred binds t=100, caller has t=0.
 * Result: 0 + 100 + 10 = 110. */
static int test_staged_hygiene_nested_macros(void) {
    const char *src =
        "defmacro add_ten {x} {\n"
        "  syntax-quote {\n"
        "    mut t 10\n"
        "    [+ ~x $t]\n"
        "  }\n"
        "}\n"
        "defmacro add_hundred {x} {\n"
        "  syntax-quote {\n"
        "    mut t 100\n"
        "    [add_ten [+ ~x $t]]\n"
        "  }\n"
        "}\n"
        "proc run {} {\n"
        "  mut t 0\n"
        "  add_hundred $t\n"
        "}\n"
        "run";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 110);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged hygiene nested: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 110);

    TEST_PASS();
}

/* Test: global builtins (like +) still resolve through macro marks. */
static int test_staged_hygiene_global_fallback(void) {
    const char *src =
        "defmacro double {x} {\n"
        "  syntax-quote [+ ~x ~x]\n"
        "}\n"
        "double 21";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 42);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged global fallback: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 42);

    TEST_PASS();
}

/* Test: ^name in macro body creates binding in caller's scope (caret hygiene).
 * [mut ^it ~v] puts `it` at scope mark 0 so caller's $it sees it. */
static int test_staged_hygiene_caret(void) {
    const char *src =
        "defmacro bindit {v} {\n"
        "  syntax-quote [mut ^it ~v]\n"
        "}\n"
        "proc run {} {\n"
        "  bindit 42\n"
        "  $it\n"
        "}\n"
        "run";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 42);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged caret: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 42);

    TEST_PASS();
}

/* Test: ^it overrides a pre-existing `it` in caller's scope. */
static int test_staged_hygiene_caret_override(void) {
    const char *src =
        "defmacro collide {x} {\n"
        "  syntax-quote [mut ^it ~x]\n"
        "}\n"
        "proc run {} {\n"
        "  mut it 999\n"
        "  collide 7\n"
        "  $it\n"
        "}\n"
        "run";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 7);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged caret override: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 7);

    TEST_PASS();
}

/* Test: unless macro on staged path (two-level expansion) */
static int test_staged_hygiene_unless(void) {
    const char *src =
        "mut x 0\n"
        "defmacro unless {cond, body} {\n"
        "  syntax-quote [if ~cond {} ~body]\n"
        "}\n"
        "unless [== 1 2] { set x 42 }\n"
        "$x";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 42);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged unless: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 42);

    TEST_PASS();
}

/* Test: quadruple macro (transitive composition) on staged path */
static int test_staged_hygiene_transitive(void) {
    const char *src =
        "defmacro double {x} {\n"
        "  syntax-quote [+ ~x ~x]\n"
        "}\n"
        "defmacro quadruple {x} {\n"
        "  syntax-quote [double [double ~x]]\n"
        "}\n"
        "quadruple 5";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 20);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged transitive: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 20);

    TEST_PASS();
}

/* ===== US-010 (macro-eval): gensym as a VM builtin on the staged path ===== */

/* Test: staged macro using [gensym] produces a working binding */
static int test_staged_gensym_basic(void) {
    const char *src =
        "defmacro with_tmp {body} {\n"
        "  syntax-quote {\n"
        "    mut [gensym] 42\n"
        "    ~body\n"
        "  }\n"
        "}\n"
        "proc run {} {\n"
        "  with_tmp { 99 }\n"
        "}\n"
        "run";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT(jacl_is_i32(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 99);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_gensym_basic: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 99);

    TEST_PASS();
}

/* Test: staged macro using [gensym "prefix"] with a long prefix;
 * generated binding does not collide with caller variable of same prefix. */
static int test_staged_gensym_long_prefix_no_collision(void) {
    const char *src =
        "defmacro bindit {v} {\n"
        "  syntax-quote [mut [gensym \"tmp_var\"] ~v]\n"
        "}\n"
        "proc run {} {\n"
        "  mut tmp_var 99\n"
        "  bindit 7\n"
        "  $tmp_var\n"
        "}\n"
        "run";

    /* Legacy path */
    JaclVM* vm_old = jacl_vm_new();
    JaclVal r_old = jacl_eval(vm_old, src);
    ASSERT(!jacl_is_error(r_old));
    ASSERT(jacl_is_i32(r_old));
    ASSERT_INT_EQ(jacl_as_i32(r_old), 99);
    jacl_vm_free(vm_old);

    /* Staged path */
    JaclError err;
    JaclVal r_new = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_gensym_long_prefix: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r_new));
    ASSERT_INT_EQ(jacl_as_i32(r_new), 99);

    TEST_PASS();
}

/* Test: staged macro using [gensym "idx"] where generated var is read back */
static int test_staged_gensym_binding_usable(void) {
    const char *src =
        "defmacro wrap {v} {\n"
        "  syntax-quote {\n"
        "    def [gensym \"idx\"] ~v\n"
        "    100\n"
        "  }\n"
        "}\n"
        "proc run {} {\n"
        "  wrap 42\n"
        "}\n"
        "run";

    /* Staged path */
    JaclError err;
    JaclVal r = staged_run(src, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "staged_gensym_binding_usable: %s\n", err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(r));
    ASSERT_INT_EQ(jacl_as_i32(r), 100);

    TEST_PASS();
}

/* ===== Procedural macros: syntax introspection in macro bodies ===== */

/* Test: macro that branches on syntax-kind of its argument.
 * If the argument is a literal integer, wrap it in [+ x 1].
 * Otherwise, return the argument unchanged. */
static int test_procedural_macro_syntax_kind(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    /* Macro that branches on syntax-kind using multi-line if...else.
     * If arg is a lit-int, double it via make-syntax.
     * Otherwise, pass through unchanged. */
    const char *src =
        "defmacro maybe-double {x} {\n"
        "  if [== [syntax-kind $x] \"lit-int\"] {\n"
        "    [make-syntax \"lit-int\" [* [syntax-datum $x] 2]]\n"
        "  } else {\n"
        "    $x\n"
        "  }\n"
        "}\n"
        "[print [maybe-double 21]]\n"       /* lit-int 21 → lit-int 42 */
        "[print [maybe-double [+ 10 5]]]";  /* command → pass-through → 15 */

    JaclError err;
    jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "procedural_syntax_kind: %s\n",
                err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "42\n15\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: macro that uses syntax-datum to extract a literal value. */
static int test_procedural_macro_syntax_datum(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src =
        "defmacro double-lit {x} {\n"
        "  def val [syntax-datum $x]\n"
        "  make-syntax \"lit-int\" [* $val 2]\n"
        "}\n"
        "[print [double-lit 21]]";  /* extract 21, make lit-int 42 */

    JaclError err;
    jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "procedural_syntax_datum: %s\n",
                err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "42\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: macro that uses syntax-head and syntax-args for command decomposition. */
static int test_procedural_macro_syntax_decompose(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    /* Macro that reverses the arguments of a 2-arg command.
     * [flip [+ 10 3]] → [+ 3 10] → 13 */
    const char *src =
        "defmacro flip {x} {\n"
        "  def h [syntax-head $x]\n"
        "  def a [syntax-args $x]\n"
        "  make-syntax \"command\" $h [vec [vec-get $a 1] [vec-get $a 0]]\n"
        "}\n"
        "[print [flip [- 10 3]]]";  /* [- 3 10] → -7 */

    JaclError err;
    jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "procedural_syntax_decompose: %s\n",
                err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "-7\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: lambda macro — takes a body expression and wraps it in
 * [proc "" [it] { body }], replicating the [\ ...] parser shorthand. */
static int test_procedural_macro_lambda(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    /* Build [proc _ {it} { body }] using make-syntax.
     * proc inside [...] is rejected by the parser, so we construct
     * the syntax object manually. */
    /* Test 1: verify proc with 'it' param works directly */
    const char *src1 =
        "proc add10 {it} { [+ $it 10] }\n"
        "[print [add10 32]]";

    JaclError err1;
    jacl_ctx_run_source(ctx, src1, strlen(src1), UINT64_MAX, &err1);
    if (err1.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "proc_direct: %s\n", err1.message ? err1.message : "(null)");
    }
    ASSERT(err1.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "42\n");

    /* Test 2: lam macro generating the same proc.
     * The param name comes from the caller so it shares scope_mark=0
     * with the body's var-refs — this is what makes hygiene work correctly. */
    jacl_ctx_destroy(ctx);
    ctx = jacl_ctx_new(NULL);
    cap.len = 0;
    cap.buf[0] = '\0';
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src =
        "defmacro lam {param body} {\n"
        "  def params [make-syntax \"command\" $param [vec]]\n"
        "  def blk [make-syntax \"block\" [vec $body]]\n"
        "  def name [make-syntax \"lit-string\" \"\"]\n"
        "  [make-syntax \"command\" [make-syntax \"lit-string\" \"proc\"] [vec $name $params $blk]]\n"
        "}\n"
        "[print [[lam x [+ $x 10]] 32]]";

    JaclError err;
    jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "procedural_macro_lambda: %s\n",
                err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "42\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: built-in \ macro (lambda shorthand, no explicit defmacro needed) */
static int test_builtin_lambda_macro(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    /* [\ + $it 10] should expand to [proc "" (it) { [+ $it 10] }] via macro */
    const char *src = "[print [[\\  + $it 10] 32]]";

    JaclError err;
    jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "builtin_lambda: %s\n",
                err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "42\n");

    /* Test 2: lambda with map-style usage */
    jacl_ctx_destroy(ctx);
    ctx = jacl_ctx_new(NULL);
    cap.len = 0;
    cap.buf[0] = '\0';
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src2 =
        "def f [\\ * $it $it]\n"
        "[print [f 7]]";

    JaclError err2;
    jacl_ctx_run_source(ctx, src2, strlen(src2), UINT64_MAX, &err2);
    if (err2.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "builtin_lambda2: %s\n",
                err2.message ? err2.message : "(null)");
    }
    ASSERT(err2.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "49\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* Test: variadic defmacro */
static int test_variadic_defmacro(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    /* Variadic macro that collects all args into a vec and counts them */
    const char *src =
        "defmacro count-args {..args} {\n"
        "  [make-syntax \"lit-int\" [vec-len $args]]\n"
        "}\n"
        "[print [count-args a b c d]]";

    JaclError err;
    jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    if (err.kind != JACL_ERROR_NONE) {
        fprintf(stderr, "variadic_macro: %s\n",
                err.message ? err.message : "(null)");
    }
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "4\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== Context save/enter/restore API ===== */

/* Test: jacl_ctx_save + jacl_ctx_restore round-trips gc__current_heap */
static int test_ctx_save_restore_basic(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    /* gc__current_heap is now &vm.heap */

    ThreadHeap *original_heap = gc__current_heap;
    ASSERT(original_heap == &vm.heap);

    jacl_ctx_saved_t saved;
    jacl_ctx_save(&saved);
    ASSERT(saved.heap == original_heap);

    /* Create a context — this overwrites gc__current_heap */
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(gc__current_heap == &ctx->vm.heap);
    ASSERT(gc__current_heap != original_heap);

    /* Destroy the context — this NULLs gc__current_heap */
    jacl_ctx_destroy(ctx);

    /* Restore — this brings back the original heap */
    jacl_ctx_restore(saved);
    ASSERT(gc__current_heap == original_heap);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: jacl_ctx_enter switches gc__current_heap to the context's heap */
static int test_ctx_enter_switches_heap(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    ThreadHeap *original_heap = gc__current_heap;

    jacl_context_t *ctx = jacl_ctx_new(NULL);
    /* gc__current_heap is now ctx's heap (from vm_init inside ctx_new) */

    /* Restore to original first */
    gc__current_heap = original_heap;
    ASSERT(gc__current_heap == original_heap);

    /* Now enter the context */
    jacl_ctx_saved_t saved;
    jacl_ctx_enter(ctx, &saved);
    ASSERT(saved.heap == original_heap);
    ASSERT(gc__current_heap == &ctx->vm.heap);

    /* Restore */
    jacl_ctx_restore(saved);
    ASSERT(gc__current_heap == original_heap);

    jacl_ctx_destroy(ctx);
    gc__current_heap = original_heap;  /* clean up after destroy */
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nested save/restore correctly stacks */
static int test_ctx_nested_save_restore(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    ThreadHeap *original_heap = gc__current_heap;

    /* Save outer state before creating first context */
    jacl_ctx_saved_t saved_outer;
    jacl_ctx_save(&saved_outer);

    jacl_context_t *outer = jacl_ctx_new(NULL);
    ThreadHeap *outer_heap = &outer->vm.heap;
    ASSERT(gc__current_heap == outer_heap);

    /* Save outer context state before creating inner context */
    jacl_ctx_saved_t saved_inner;
    jacl_ctx_save(&saved_inner);

    jacl_context_t *inner = jacl_ctx_new(NULL);
    ThreadHeap *inner_heap = &inner->vm.heap;
    ASSERT(gc__current_heap == inner_heap);

    /* Destroy inner, restore to outer */
    jacl_ctx_destroy(inner);
    jacl_ctx_restore(saved_inner);
    ASSERT(gc__current_heap == outer_heap);

    /* Destroy outer, restore to original */
    jacl_ctx_destroy(outer);
    jacl_ctx_restore(saved_outer);
    ASSERT(gc__current_heap == original_heap);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: error path in ast_expand_macros restores gc__current_heap */
static int test_ctx_error_path_restores_heap(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable it;
    intern_table_init(&it, &arena);
    ThreadHeap *original_heap = gc__current_heap;

    /* Source with duplicate macro definitions → expansion error. */
    const char *src =
        "defmacro foo {x} { $x }\n"
        "defmacro foo {x} { $x }\n";
    LexResult tokens = lexer_lex(src, &arena);
    ParseResult parse = parser_parse(tokens, &arena);
    ASSERT(parse.error_count == 0);

    MacroTable mt;
    macro_table_init(&mt);
    ExpandState es;
    memset(&es, 0, sizeof(es));

    uint32_t err_line = 0, err_col = 0;
    const char *err = ast_expand_macros(parse.nodes, parse.count, &mt,
        &vm.heap, &it, &arena, &es, &err_line, &err_col);

    /* Should have an error about shadowing */
    ASSERT(err != NULL);

    /* Critical: gc__current_heap must be restored after error */
    ASSERT(gc__current_heap == original_heap);

    intern_table_destroy(&it);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: jacl_run correctly restores gc__current_heap */
static int test_jacl_run_restores_heap(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    ThreadHeap *original_heap = gc__current_heap;

    VMResult r = jacl_run("[+ 1 2]", &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT(jacl_as_i32(vm.stack[0]) == 3);

    /* gc__current_heap must be restored to the caller's heap */
    ASSERT(gc__current_heap == original_heap);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: macro expansion through jacl_ctx_run_source restores heap */
static int test_ctx_run_with_macros_restores_heap(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    ThreadHeap *caller_heap = gc__current_heap;

    jacl_context_t *ctx = jacl_ctx_new(NULL);

    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    const char *src =
        "def f [\\ + $it 10]\n"
        "[print [f 5]]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT_STR_EQ(cap.buf, "15\n");
    (void)result;

    jacl_ctx_destroy(ctx);

    /* Caller's heap must be restorable */
    gc__current_heap = caller_heap;
    ASSERT(gc__current_heap == caller_heap);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: multiple sequential context runs don't corrupt heap state */
static int test_ctx_sequential_runs(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    ThreadHeap *original_heap = gc__current_heap;

    /* Run 1: simple arithmetic */
    VMResult r1 = jacl_run("[+ 10 20]", &vm, &arena);
    ASSERT(r1 == VM_OK);
    ASSERT(jacl_as_i32(vm.stack[0]) == 30);
    ASSERT(gc__current_heap == original_heap);

    /* Run 2: with lambda macro */
    VMResult r2 = jacl_run("def f [\\ * $it 3]\n[f 7]", &vm, &arena);
    ASSERT(r2 == VM_OK);
    ASSERT(jacl_as_i32(vm.stack[0]) == 21);
    ASSERT(gc__current_heap == original_heap);

    /* Run 3: with user defmacro */
    VMResult r3 = jacl_run(
        "defmacro double {x} { syntax-quote [+ ~x ~x] }\n[double 5]",
        &vm, &arena);
    ASSERT(r3 == VM_OK);
    ASSERT(jacl_as_i32(vm.stack[0]) == 10);
    ASSERT(gc__current_heap == original_heap);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-004: Shell Interop - exec returns stdout stream ===== */

/* Test: basic !echo command collects to string */
static int test_shell_exec_echo_basic(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Run !echo "hello" | collect and print the result */
    VMResult result = jacl_run("[print {!echo hello | collect}]", &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    /* echo outputs "hello\n" */
    ASSERT_STR_EQ(cap.buf, "hello\n\n");  /* output + print newline */

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: !echo with multiple word args passes correctly */
static int test_shell_exec_multiple_args(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Run !echo with multiple args */
    VMResult result = jacl_run("[print {!echo one two three | collect}]", &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    /* echo outputs "one two three\n" */
    ASSERT_STR_EQ(cap.buf, "one two three\n\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: [exec "cmd" args...] direct call works */
static int test_shell_exec_direct_call(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult result = jacl_run("[print {[exec \"echo\" \"world\"] | collect}]", &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    ASSERT_STR_EQ(cap.buf, "world\n\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: exec stream is lazy - can iterate line by line */
static int test_shell_exec_stream_lazy(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* printf outputs two lines, we take the first */
    VMResult result = jacl_run(
        "[print {!printf \"line1\\nline2\\n\" | first}]",
        &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    ASSERT_STR_EQ(cap.buf, "line1\n\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: child process inherits working directory */
static int test_shell_exec_inherits_env(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* pwd should output the current working directory - just verify non-empty */
    VMResult result = jacl_run(
        "[print {!pwd | collect}]",
        &vm, &arena);

    /* Should succeed and output should have non-zero length */
    ASSERT_INT_EQ(result, VM_OK);
    /* The output should contain at least "/" (the root) */
    ASSERT(cap.len > 2);  /* "/something\n\n" at minimum */

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ===== US-005: Shell Interop - error handling (exit codes) ===== */

/* Test: !false returns error value (exit 1) */
static int test_shell_exec_error_false(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult result = jacl_run(
        "[print [error? {!false | collect}]]",
        &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    ASSERT_STR_EQ(cap.buf, "true\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: error message contains stderr content */
static int test_shell_exec_error_stderr(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Run a command that outputs to stderr and exits non-zero
     * Note: JACL uses double quotes, not single quotes */
    VMResult result = jacl_run(
        "def e {!sh \"-c\" \"echo err >&2; exit 1\" | collect}\n"
        "[print [error-val $e]]",
        &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    /* Error message should contain "err" from stderr */
    ASSERT(strstr(cap.buf, "err") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: successful commands return string, not error */
static int test_shell_exec_success_no_error(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult result = jacl_run(
        "[print [error? {!true | collect}]]",
        &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    ASSERT_STR_EQ(cap.buf, "false\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: error composes with pipes - for loop doesn't run on error */
static int test_shell_exec_error_pipe_compose(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* If for runs on error stream, it would print "ran"
     * Since the stream returns an error value, the for should not iterate */
    VMResult result = jacl_run(
        "def result {!false | for x { [print ran] }}\n"
        "[print [error? $result]]",
        &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    /* "ran" should NOT appear, but error? should be true */
    ASSERT(strstr(cap.buf, "ran") == NULL);
    ASSERT(strstr(cap.buf, "true") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: error works with try/catch */
static int test_shell_exec_error_try_catch(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult result = jacl_run(
        "def result [try { {!false | collect} } e { \"fallback\" }]\n"
        "[print $result]",
        &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    ASSERT_STR_EQ(cap.buf, "fallback\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test: exit code 0 returns stdout content */
static int test_shell_exec_success_returns_stdout(void) {
    tracker_reset();
    arena_t arena = { .allocator = tracked_allocator };

    PrintCapture cap = { .len = 0 };
    VM vm;
    vm_init(&vm, &arena);
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult result = jacl_run(
        "[print {!echo hello | collect}]",
        &vm, &arena);

    ASSERT_INT_EQ(result, VM_OK);
    /* Should have "hello\n\n" (echo output + print's newline) */
    ASSERT(strstr(cap.buf, "hello") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-010: End-to-end pipeline integration */
    { "jacl_run_basic",          test_jacl_run_basic },
    { "motivating_example",      test_motivating_example },
    { "parse_error_reported",    test_parse_error_reported },
    { "compile_error_reported",  test_compile_error_reported },
    { "runtime_error_with_line", test_runtime_error_with_line },
    { "deliberate_errors",       test_deliberate_errors },
    { "comprehensive_program",   test_comprehensive_program },
    { "f32_pipeline",            test_f32_pipeline },
    { "semicolons_pipeline",     test_semicolons_pipeline },
    { "empty_program",           test_empty_program },
    /* US-003 (M6): Comma as command separator */
    { "comma_sep_toplevel",      test_comma_separator_toplevel },
    { "comma_sep_block",         test_comma_separator_block },
    /* US-005 (M6): $var lines as command calls */
    { "var_ref_cmd_call",        test_var_ref_command_call },
    /* US-005 (macro-eval): context lifecycle and reentrancy */
    { "context_lifecycle",       test_context_lifecycle },
    { "recursive_vm_exec",       test_recursive_vm_exec },
    /* US-006 (macro-eval): jacl_ctx_run API */
    { "ctx_run_source_valid",    test_ctx_run_source_valid },
    { "ctx_run_source_parse_err",test_ctx_run_source_parse_error },
    { "ctx_run_source_rt_err",   test_ctx_run_source_runtime_error },
    { "ctx_run_source_nested",   test_ctx_run_source_nested },
    /* M14 (interpret): [interpret $src] builtin — Phase 1 */
    { "interpret_basic",         test_interpret_basic },
    { "interpret_string_result", test_interpret_string_result },
    { "interpret_parse_error",   test_interpret_parse_error },
    { "interpret_runtime_error", test_interpret_runtime_error },
    { "interpret_with_print",    test_interpret_with_print },
    { "interpret_type_error",    test_interpret_type_error },
    { "interpret_prelude_type",  test_interpret_prelude_type_error },
    { "interpret_shared_heap",   test_interpret_shared_heap },
    { "interpret_vector_result", test_interpret_vector_result },
    { "interpret_map_result",    test_interpret_map_result },
    /* source_to_closure_in_place */
    { "stc_valid",               test_source_to_closure_valid },
    { "stc_parse_error",         test_source_to_closure_parse_error },
    { "stc_compile_error",       test_source_to_closure_compile_error },
    /* US-003: Compiler prelude map as compile-time env seed */
    { "prelude_compile_ok",      test_prelude_compile_success },
    { "prelude_compile_unresol", test_prelude_compile_error_unresolved },
    { "prelude_entries_reach",   test_prelude_entries_reachable },
    /* US-004: [interpret-prelude] builtin */
    { "iprelude_has_interpret",  test_interpret_prelude_has_interpret },
    { "iprelude_no_core",        test_interpret_prelude_no_core },
    { "iprelude_has_print",      test_interpret_prelude_has_print },
    { "iprelude_native_fn_vals", test_interpret_prelude_native_fn_values },
    { "iprelude_direct_opcode",  test_interpret_prelude_direct_opcode },
    { "iprelude_override",       test_interpret_prelude_override_builtin },
    /* US-005 (sandboxed eval): [interpret $prelude $src] two-arg form */
    { "interp2_prelude_var",     test_interpret_two_arg_prelude_var },
    { "interp2_empty_core",      test_interpret_two_arg_empty_prelude_core },
    { "interp2_blocked_print",   test_interpret_two_arg_blocked_print },
    { "interp2_blocked_nested",  test_interpret_two_arg_blocked_nested },
    { "interp2_custom_closure",  test_interpret_two_arg_custom_closure },
    { "interp2_shared_state",    test_interpret_two_arg_shared_state },
    /* US-006 (sandboxed eval): non-core builtin opcode-vs-env downgrade */
    { "sandbox_print_downgrade", test_sandbox_print_downgrade },
    { "sandbox_spawn_blocked",   test_sandbox_spawn_blocked },
    { "sandbox_core_arith",      test_sandbox_core_arithmetic },
    { "sandbox_macro_noncore",   test_sandbox_macro_noncore },
    /* US-007 (macro-eval): staged syntax-quote compilation */
    { "staged_sq_literal",       test_staged_sq_literal },
    { "staged_sq_unquote",       test_staged_sq_unquote },
    { "staged_sq_splice",        test_staged_sq_splice },
    { "staged_sq_nested",        test_staged_sq_nested },
    /* US-008 (macro-eval): staged macro expansion */
    { "staged_macro_e2e",        test_staged_macro_e2e },
    { "staged_macro_block",      test_staged_macro_block },
    { "staged_macro_fwd_ref",    test_staged_macro_forward_ref },
    /* US-009 (macro-eval): mark-propagation hygiene on staged path */
    { "staged_hygiene_shadow",   test_staged_hygiene_local_shadow },
    { "staged_hygiene_nested",   test_staged_hygiene_nested_macros },
    { "staged_hygiene_global",   test_staged_hygiene_global_fallback },
    { "staged_hygiene_caret",    test_staged_hygiene_caret },
    { "staged_hygiene_caret_ovr",test_staged_hygiene_caret_override },
    { "staged_hygiene_unless",   test_staged_hygiene_unless },
    { "staged_hygiene_trans",    test_staged_hygiene_transitive },
    /* US-010 (macro-eval): gensym as VM builtin on staged path */
    { "staged_gensym_basic",     test_staged_gensym_basic },
    { "staged_gensym_long_pfx",  test_staged_gensym_long_prefix_no_collision },
    { "staged_gensym_bind_use",  test_staged_gensym_binding_usable },
    /* Procedural macros: syntax introspection in macro bodies */
    { "proc_macro_kind",         test_procedural_macro_syntax_kind },
    { "proc_macro_datum",        test_procedural_macro_syntax_datum },
    { "proc_macro_decompose",    test_procedural_macro_syntax_decompose },
    { "proc_macro_lambda",       test_procedural_macro_lambda },
    /* Built-in \ macro and variadic macros */
    { "builtin_lambda",          test_builtin_lambda_macro },
    { "variadic_defmacro",       test_variadic_defmacro },
    /* Context save/enter/restore API */
    { "ctx_save_restore_basic",  test_ctx_save_restore_basic },
    { "ctx_enter_switches_heap", test_ctx_enter_switches_heap },
    { "ctx_nested_save_restore", test_ctx_nested_save_restore },
    { "ctx_error_restores_heap", test_ctx_error_path_restores_heap },
    { "jacl_run_restores_heap",  test_jacl_run_restores_heap },
    { "ctx_run_macros_heap",     test_ctx_run_with_macros_restores_heap },
    { "ctx_sequential_runs",     test_ctx_sequential_runs },
    /* US-004 (Shell Interop): exec returns stdout stream */
    { "shell_exec_echo_basic",   test_shell_exec_echo_basic },
    { "shell_exec_multi_args",   test_shell_exec_multiple_args },
    { "shell_exec_direct_call",  test_shell_exec_direct_call },
    { "shell_exec_stream_lazy",  test_shell_exec_stream_lazy },
    { "shell_exec_inherits_env", test_shell_exec_inherits_env },
    /* US-005 (Shell Interop): error handling - exit codes */
    { "shell_exec_err_false",    test_shell_exec_error_false },
    { "shell_exec_err_stderr",   test_shell_exec_error_stderr },
    { "shell_exec_success_ok",   test_shell_exec_success_no_error },
    { "shell_exec_err_pipe",     test_shell_exec_error_pipe_compose },
    { "shell_exec_err_try",      test_shell_exec_error_try_catch },
    { "shell_exec_success_out",  test_shell_exec_success_returns_stdout },
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
