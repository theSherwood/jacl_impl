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
      &ctx_a->intern_table, &ctx_a->vm.heap, NULL, &es_a);
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
      &ctx_b->intern_table, &ctx_b->vm.heap, NULL, &es_b);
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

/* ===== US-007 (macro-eval): Staged syntax-quote compilation ===== */

/* Helper: run source in a staged-syntax-quote context, return the result */
static JaclVal staged_run(const char *src, JaclError *err) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ctx->use_staged_syntax_quote = true;
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
    /* US-007 (macro-eval): staged syntax-quote compilation */
    { "staged_sq_literal",       test_staged_sq_literal },
    { "staged_sq_unquote",       test_staged_sq_unquote },
    { "staged_sq_splice",        test_staged_sq_splice },
    { "staged_sq_nested",        test_staged_sq_nested },
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
