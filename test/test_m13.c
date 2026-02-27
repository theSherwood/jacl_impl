/*
 * test_m13.c — M13 Future type and GC integration tests (US-001)
 *
 * Tests for JaclFuture creation, resolution, error, waiter registration,
 * GC tracing of futures and waiters, and the future? builtin predicate.
 */

#include "test_helpers.h"
#include "../src/jacl.c"

/* --- PrintCapture helper (same as other integration tests) --- */

typedef struct {
    char     buffer[1024];
    uint32_t len;
} PrintCapture;

static void capture_print(const char *text, uint32_t len, void *ctx) {
    PrintCapture *cap = (PrintCapture *)ctx;
    uint32_t space = (uint32_t)sizeof(cap->buffer) - cap->len - 1;
    if (len > space) len = space;
    memcpy(cap->buffer + cap->len, text, len);
    cap->len += len;
    cap->buffer[cap->len] = '\0';
}

/* ====================================================================
 * US-001: Future type and GC integration
 * ==================================================================== */

/* Test: create a pending future, check type tag */
static int test_future_create_pending(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    ASSERT(jacl_is_future(f));
    ASSERT(!jacl_is_nil(f));
    ASSERT(!jacl_is_closure(f));

    JaclFuture *fut = jacl_as_future(f);
    ASSERT_U32_EQ(ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED), FUTURE_PENDING);
    ASSERT(fut->waiters == NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: resolve a future, check state and result */
static int test_future_resolve(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    JaclVal result = jacl_i32(42);
    FutureWaiter *waiters = jacl_future_resolve(fut, result, NULL, NULL);

    ASSERT(waiters == NULL); /* no waiters registered */
    ASSERT_U32_EQ(ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED), FUTURE_RESOLVED);
    ASSERT_U64_EQ((JaclVal)fut->result, result);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: error a future, check state and error value */
static int test_future_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    JaclVal err = jacl_set_error(jacl_i32(99));
    FutureWaiter *waiters = jacl_future_error(fut, err, NULL, NULL);

    ASSERT(waiters == NULL);
    ASSERT_U32_EQ(ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED), FUTURE_ERROR);
    ASSERT_U64_EQ((JaclVal)fut->result, err);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: add waiter to pending future — returns true */
static int test_future_add_waiter_pending(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    /* Use a simple value as placeholder continuation */
    JaclVal cont = jacl_i32(100);
    bool added = jacl_future_add_waiter(fut, cont, &vm.heap);
    ASSERT(added);
    ASSERT(fut->waiters != NULL);
    ASSERT_U64_EQ(fut->waiters->continuation, cont);
    ASSERT(fut->waiters->next == NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: add waiter to already-resolved future — returns false */
static int test_future_add_waiter_resolved(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    jacl_future_resolve(fut, jacl_i32(42), NULL, NULL);

    JaclVal cont = jacl_i32(200);
    bool added = jacl_future_add_waiter(fut, cont, &vm.heap);
    ASSERT(!added); /* should return false — already resolved */

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: add waiter to already-errored future — returns false */
static int test_future_add_waiter_errored(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    jacl_future_error(fut, jacl_set_error(jacl_i32(1)), NULL, NULL);

    JaclVal cont = jacl_i32(300);
    bool added = jacl_future_add_waiter(fut, cont, &vm.heap);
    ASSERT(!added);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: multiple waiters, then resolve — returns all waiters */
static int test_future_multiple_waiters(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    JaclVal c1 = jacl_i32(10);
    JaclVal c2 = jacl_i32(20);
    JaclVal c3 = jacl_i32(30);

    ASSERT(jacl_future_add_waiter(fut, c1, &vm.heap));
    ASSERT(jacl_future_add_waiter(fut, c2, &vm.heap));
    ASSERT(jacl_future_add_waiter(fut, c3, &vm.heap));

    FutureWaiter *waiters = jacl_future_resolve(fut, jacl_i32(99), NULL, NULL);

    /* Waiters should be in reverse order (prepend) */
    int count = 0;
    FutureWaiter *w = waiters;
    while (w) {
        count++;
        w = w->next;
    }
    ASSERT_INT_EQ(count, 3);

    /* c3 was prepended last, so it should be first */
    ASSERT_U64_EQ(waiters->continuation, c3);
    ASSERT_U64_EQ(waiters->next->continuation, c2);
    ASSERT_U64_EQ(waiters->next->next->continuation, c1);

    /* After resolve, waiters list should be cleared */
    ASSERT(fut->waiters == NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: GC traces live future with resolved result */
static int test_gc_trace_future_resolved(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Create a future and resolve it with a heap value */
    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    JaclVal result = jacl_i64(&vm.heap, 12345678LL);
    jacl_future_resolve(fut, result, NULL, NULL);

    /* Root the future on the VM stack */
    vm.stack[0] = f;
    vm.stack_top = 1;

    /* Run GC */
    gc_collect(&vm.heap, &vm);

    /* Future and its result should survive */
    ASSERT(gc_header_of(jacl_as_ptr(f))->mark == (1 - vm.heap.current_mark));
    ASSERT(gc_header_of(jacl_as_ptr(result))->mark == (1 - vm.heap.current_mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: GC traces live future with pending waiters */
static int test_gc_trace_future_waiters(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    /* Add a waiter with a heap value continuation */
    JaclVal cont = jacl_i64(&vm.heap, 99999LL);
    jacl_future_add_waiter(fut, cont, &vm.heap);

    /* Root the future */
    vm.stack[0] = f;
    vm.stack_top = 1;

    /* Run GC */
    gc_collect(&vm.heap, &vm);

    /* Future, waiter node, and continuation should all survive.
     * Mark was toggled by gc_collect, so current_mark is now the OPPOSITE
     * of what was used during marking. Objects marked by gc_mark use
     * the mark value BEFORE toggling. */
    uint8_t mark_used = 1 - vm.heap.current_mark;
    ASSERT(gc_header_of(jacl_as_ptr(f))->mark == mark_used);
    ASSERT(gc_header_of(fut->waiters)->mark == mark_used);
    ASSERT(gc_header_of(jacl_as_ptr(cont))->mark == mark_used);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: jacl_is_future predicate */
static int test_is_future_predicate(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal f = jacl_future(&vm.heap);
    ASSERT(jacl_is_future(f));
    ASSERT(!jacl_is_future(JACL_NIL));
    ASSERT(!jacl_is_future(JACL_TRUE));
    ASSERT(!jacl_is_future(jacl_i32(42)));

    /* future? in the value type system */
    ASSERT(jacl_is_heap_type(f));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: future? builtin via compiler + VM */
static int test_future_predicate_builtin(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Create a future manually and put it in the environment */
    JaclVal f = jacl_future(&vm.heap);
    vm.env.names[vm.env.count]  = jacl_inline_string("myf", 3);
    vm.env.values[vm.env.count] = f;
    vm.env.count++;

    /* Compile and run: [future? $myf] */
    VMResult r = jacl_run("print [future? $myf]", &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "true\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: future formatting via print */
static int test_future_print_format(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Create pending future, put in env, print it */
    JaclVal f = jacl_future(&vm.heap);
    vm.env.names[vm.env.count]  = jacl_inline_string("myf", 3);
    vm.env.values[vm.env.count] = f;
    vm.env.count++;

    VMResult r = jacl_run("print $myf", &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "<future: pending>\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * US-002: Suspension analysis in compiler
 * ==================================================================== */

/* Helper: run suspension analysis on source, return the map */
static SuspensionMap test__analyze(const char* source) {
    arena_t arena = {0};
    LexResult tokens = lexer_lex(source, &arena);
    ParseResult parse = parser_parse(tokens, &arena);
    SuspensionMap map = compiler__analyze_suspension(parse.nodes, parse.count);
    arena_destroy(&arena);
    return map;
}

/* Helper: compile source, return true if compile error occurred */
static bool test__compile_has_error(const char* source, const char* expected_substr) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    LexResult tokens = lexer_lex(source, &arena);
    ParseResult parse = parser_parse(tokens, &arena);
    JaclInternTable intern_table;
    intern_table_init(&intern_table, &arena);
    CompileResult cr = compiler_compile(parse, &arena, &intern_table, &vm.heap);

    bool has_error = (cr.error_count > 0);
    bool has_substr = false;
    if (has_error && expected_substr && cr.error_message) {
        has_substr = (strstr(cr.error_message, expected_substr) != NULL);
    }

    vm_destroy(&vm);
    arena_destroy(&arena);

    if (expected_substr) return has_error && has_substr;
    return has_error;
}

/* Test: suspension inferred for direct use of await */
static int test_suspension_direct_await(void) {
    SuspensionMap map = test__analyze(
        "proc foo [] { await 42 }");
    JaclVal foo = jacl_inline_string("foo", 3);
    ASSERT(suspension_map_lookup(&map, foo));
    TEST_PASS();
}

/* Test: suspension inferred for direct use of parallel */
static int test_suspension_direct_parallel(void) {
    SuspensionMap map = test__analyze(
        "proc foo [] { parallel { 1 } { 2 } }");
    JaclVal foo = jacl_inline_string("foo", 3);
    ASSERT(suspension_map_lookup(&map, foo));
    TEST_PASS();
}

/* Test: suspension inferred for direct use of race */
static int test_suspension_direct_race(void) {
    SuspensionMap map = test__analyze(
        "proc foo [] { race { 1 } { 2 } }");
    JaclVal foo = jacl_inline_string("foo", 3);
    ASSERT(suspension_map_lookup(&map, foo));
    TEST_PASS();
}

/* Test: transitive suspension through direct calls */
static int test_suspension_transitive(void) {
    SuspensionMap map = test__analyze(
        "proc inner [] { await 42 }\n"
        "proc outer [] { inner }");
    JaclVal inner = jacl_inline_string("inner", 5);
    JaclVal outer = jacl_inline_string("outer", 5);
    ASSERT(suspension_map_lookup(&map, inner));
    ASSERT(suspension_map_lookup(&map, outer));
    TEST_PASS();
}

/* Test: nested procs — inner suspends, outer calls inner → both suspend */
static int test_suspension_nested_procs(void) {
    SuspensionMap map = test__analyze(
        "proc outer [] {\n"
        "  proc inner [] { await 42 }\n"
        "  inner\n"
        "}");
    JaclVal inner = jacl_inline_string("inner", 5);
    JaclVal outer = jacl_inline_string("outer", 5);
    ASSERT(suspension_map_lookup(&map, inner));
    ASSERT(suspension_map_lookup(&map, outer));
    TEST_PASS();
}

/* Test: indirect call to unknown closure — conservatively suspending
   when suspending procs exist in the program */
static int test_suspension_indirect_call(void) {
    SuspensionMap map = test__analyze(
        "proc async_fn [] { await 42 }\n"
        "proc caller [f] { [$f] }");
    JaclVal async_fn = jacl_inline_string("async_f", 7);
    /* async_fn name is truncated to 7 chars for inline; use the actual name */
    async_fn = jacl_inline_string("async_f", 7);
    /* Actually, proc name in AST is "async_fn" which is 8 chars — exceeds 7-byte limit.
       Let me use shorter names. */
    (void)async_fn;
    map = test__analyze(
        "proc afn [] { await 42 }\n"
        "proc caller [f] { [$f] }");
    JaclVal afn = jacl_inline_string("afn", 3);
    JaclVal caller = jacl_inline_string("caller", 6);
    ASSERT(suspension_map_lookup(&map, afn));
    ASSERT(suspension_map_lookup(&map, caller));
    TEST_PASS();
}

/* Test: proc without suspension points is non-suspending */
static int test_suspension_non_suspending(void) {
    SuspensionMap map = test__analyze(
        "proc add [a b] { [+ $a $b] }\n"
        "proc mul [a b] { [* $a $b] }");
    JaclVal add = jacl_inline_string("add", 3);
    JaclVal mul = jacl_inline_string("mul", 3);
    ASSERT(!suspension_map_lookup(&map, add));
    ASSERT(!suspension_map_lookup(&map, mul));
    TEST_PASS();
}

/* Test: spawn is NOT a suspension point */
static int test_suspension_spawn_not_suspending(void) {
    SuspensionMap map = test__analyze(
        "proc foo [] { spawn { 42 } }");
    JaclVal foo = jacl_inline_string("foo", 3);
    ASSERT(!suspension_map_lookup(&map, foo));
    TEST_PASS();
}

/* Test: indirect calls are non-suspending when no suspending procs exist */
static int test_suspension_indirect_no_suspending_procs(void) {
    SuspensionMap map = test__analyze(
        "proc caller [f] { [$f] }");
    JaclVal caller = jacl_inline_string("caller", 6);
    ASSERT(!suspension_map_lookup(&map, caller));
    TEST_PASS();
}

/* Test: compile error for await inside try/catch */
static int test_suspension_error_try_catch(void) {
    ASSERT(test__compile_has_error(
        "proc foo [] { try { await 42 } e { $e } }",
        "cannot suspend inside try/catch"));
    TEST_PASS();
}

/* Test: compile error for parallel inside try/catch */
static int test_suspension_error_try_catch_parallel(void) {
    ASSERT(test__compile_has_error(
        "proc foo [] { try { parallel { 1 } { 2 } } e { $e } }",
        "cannot suspend inside try/catch"));
    TEST_PASS();
}

/* Test: compile error for race inside try/catch */
static int test_suspension_error_try_catch_race(void) {
    ASSERT(test__compile_has_error(
        "proc foo [] { try { race { 1 } { 2 } } e { $e } }",
        "cannot suspend inside try/catch"));
    TEST_PASS();
}

/* Test: compile error for suspending closure passed to each */
static int test_suspension_error_suspending_callback_each(void) {
    ASSERT(test__compile_has_error(
        "proc sfn [x] { await $x }\n"
        "[each [vec 1 2] $sfn]",
        "cannot pass suspending closure to non-suspending builtin"));
    TEST_PASS();
}

/* Test: compile error for suspending closure passed to filter */
static int test_suspension_error_suspending_callback_filter(void) {
    ASSERT(test__compile_has_error(
        "proc sfn [x] { await $x }\n"
        "[filter [vec 1 2] $sfn]",
        "cannot pass suspending closure to non-suspending builtin"));
    TEST_PASS();
}

/* Test: compile error for suspending closure passed to transform */
static int test_suspension_error_suspending_callback_transform(void) {
    ASSERT(test__compile_has_error(
        "proc sfn [x] { await $x }\n"
        "[transform [vec 1 2] $sfn]",
        "cannot pass suspending closure to non-suspending builtin"));
    TEST_PASS();
}

/* Test: all existing code compiles without errors (no false positives) */
static int test_suspension_existing_code_compiles(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    VMResult r = jacl_run(
        "proc add [a b] { [+ $a $b] }\n"
        "proc apply [f x] { [$f $x] }\n"
        "proc double [x] { [* $x 2] }\n"
        "print [add 1 2]",
        &vm, &arena);
    ASSERT(r == VM_OK);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        /* US-001: Future type and GC integration */
        { "future_create_pending",       test_future_create_pending },
        { "future_resolve",              test_future_resolve },
        { "future_error",                test_future_error },
        { "future_add_waiter_pending",   test_future_add_waiter_pending },
        { "future_add_waiter_resolved",  test_future_add_waiter_resolved },
        { "future_add_waiter_errored",   test_future_add_waiter_errored },
        { "future_multiple_waiters",     test_future_multiple_waiters },
        { "gc_trace_future_resolved",    test_gc_trace_future_resolved },
        { "gc_trace_future_waiters",     test_gc_trace_future_waiters },
        { "is_future_predicate",         test_is_future_predicate },
        { "future_predicate_builtin",    test_future_predicate_builtin },
        { "future_print_format",         test_future_print_format },
        /* US-002: Suspension analysis in compiler */
        { "suspension_direct_await",     test_suspension_direct_await },
        { "suspension_direct_parallel",  test_suspension_direct_parallel },
        { "suspension_direct_race",      test_suspension_direct_race },
        { "suspension_transitive",       test_suspension_transitive },
        { "suspension_nested_procs",     test_suspension_nested_procs },
        { "suspension_indirect_call",    test_suspension_indirect_call },
        { "suspension_non_suspending",   test_suspension_non_suspending },
        { "suspension_spawn_not",        test_suspension_spawn_not_suspending },
        { "suspension_indirect_no_sp",   test_suspension_indirect_no_suspending_procs },
        { "susp_error_try_catch",        test_suspension_error_try_catch },
        { "susp_error_try_parallel",     test_suspension_error_try_catch_parallel },
        { "susp_error_try_race",         test_suspension_error_try_catch_race },
        { "susp_error_each_callback",    test_suspension_error_suspending_callback_each },
        { "susp_error_filter_callback",  test_suspension_error_suspending_callback_filter },
        { "susp_error_transform_cb",     test_suspension_error_suspending_callback_transform },
        { "susp_existing_code_ok",       test_suspension_existing_code_compiles },
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
    return (failed > 0) ? 1 : 0;
}
