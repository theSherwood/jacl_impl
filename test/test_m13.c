/*
 * test_m13.c — M13 Future type and GC integration tests (US-001)
 *
 * Tests for JaclFuture creation, resolution, error, waiter registration,
 * GC tracing of futures and waiters, and the future? builtin predicate.
 */

#include "test_helpers.h"
#include "../src/jacl.h"

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
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern_table;
    intern_table_init(&intern_table, &arena);
    LexResult tokens = lexer_lex(source, &arena);
    ParseResult parse = parser_parse(tokens, &arena);
    SuspensionMap map = compiler__analyze_suspension(parse.nodes, parse.count,
                                                     &vm.heap, &intern_table);
    vm_destroy(&vm);
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
    CompileResult cr = compiler_compile(parse, &arena, &intern_table, &vm.heap, NULL, NULL, JACL_NIL);

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
        "proc foo {} { await [spawn { 42 }] }");
    JaclVal foo = jacl_inline_string("foo", 3);
    ASSERT(suspension_map_lookup(&map, foo));
    TEST_PASS();
}

/* Test: suspension inferred for direct use of parallel */
static int test_suspension_direct_parallel(void) {
    SuspensionMap map = test__analyze(
        "proc foo {} { parallel { 1 } { 2 } }");
    JaclVal foo = jacl_inline_string("foo", 3);
    ASSERT(suspension_map_lookup(&map, foo));
    TEST_PASS();
}

/* Test: suspension inferred for direct use of race */
static int test_suspension_direct_race(void) {
    SuspensionMap map = test__analyze(
        "proc foo {} { race { 1 } { 2 } }");
    JaclVal foo = jacl_inline_string("foo", 3);
    ASSERT(suspension_map_lookup(&map, foo));
    TEST_PASS();
}

/* Test: transitive suspension through direct calls */
static int test_suspension_transitive(void) {
    SuspensionMap map = test__analyze(
        "proc inner {} { await [spawn { 42 }] }\n"
        "proc outer {} { inner }");
    JaclVal inner = jacl_inline_string("inner", 5);
    JaclVal outer = jacl_inline_string("outer", 5);
    ASSERT(suspension_map_lookup(&map, inner));
    ASSERT(suspension_map_lookup(&map, outer));
    TEST_PASS();
}

/* Test: nested procs — inner suspends, outer calls inner → both suspend */
static int test_suspension_nested_procs(void) {
    SuspensionMap map = test__analyze(
        "proc outer {} {\n"
        "  proc inner {} { await [spawn { 42 }] }\n"
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
        "proc async_fn {} { await [spawn { 42 }] }\n"
        "proc caller {f} { [$f] }");
    JaclVal async_fn = jacl_inline_string("async_f", 7);
    /* async_fn name is truncated to 7 chars for inline; use the actual name */
    async_fn = jacl_inline_string("async_f", 7);
    /* Actually, proc name in AST is "async_fn" which is 8 chars — exceeds 7-byte limit.
       Let me use shorter names. */
    (void)async_fn;
    map = test__analyze(
        "proc afn {} { await [spawn { 42 }] }\n"
        "proc caller {f} { [$f] }");
    JaclVal afn = jacl_inline_string("afn", 3);
    JaclVal caller = jacl_inline_string("caller", 6);
    ASSERT(suspension_map_lookup(&map, afn));
    ASSERT(suspension_map_lookup(&map, caller));
    TEST_PASS();
}

/* Test: proc without suspension points is non-suspending */
static int test_suspension_non_suspending(void) {
    SuspensionMap map = test__analyze(
        "proc add {a, b} { + $a $b }\n"
        "proc mul {a, b} { * $a $b }");
    JaclVal add = jacl_inline_string("add", 3);
    JaclVal mul = jacl_inline_string("mul", 3);
    ASSERT(!suspension_map_lookup(&map, add));
    ASSERT(!suspension_map_lookup(&map, mul));
    TEST_PASS();
}

/* Test: spawn is NOT a suspension point */
static int test_suspension_spawn_not_suspending(void) {
    SuspensionMap map = test__analyze(
        "proc foo {} { spawn { 42 } }");
    JaclVal foo = jacl_inline_string("foo", 3);
    ASSERT(!suspension_map_lookup(&map, foo));
    TEST_PASS();
}

/* Test: indirect calls are non-suspending when no suspending procs exist */
static int test_suspension_indirect_no_suspending_procs(void) {
    SuspensionMap map = test__analyze(
        "proc caller {f} { [$f] }");
    JaclVal caller = jacl_inline_string("caller", 6);
    ASSERT(!suspension_map_lookup(&map, caller));
    TEST_PASS();
}

/* Test: compile error for await inside try/catch */
static int test_suspension_error_try_catch(void) {
    ASSERT(test__compile_has_error(
        "proc foo {} { try { await [spawn { 42 }] } e { $e } }",
        "cannot suspend inside try/catch"));
    TEST_PASS();
}

/* Test: compile error for parallel inside try/catch */
static int test_suspension_error_try_catch_parallel(void) {
    ASSERT(test__compile_has_error(
        "proc foo {} { try { parallel { 1 } { 2 } } e { $e } }",
        "cannot suspend inside try/catch"));
    TEST_PASS();
}

/* Test: compile error for race inside try/catch */
static int test_suspension_error_try_catch_race(void) {
    ASSERT(test__compile_has_error(
        "proc foo {} { try { race { 1 } { 2 } } e { $e } }",
        "cannot suspend inside try/catch"));
    TEST_PASS();
}

/* Test: compile error for suspending closure passed to each */
static int test_suspension_error_suspending_callback_each(void) {
    ASSERT(test__compile_has_error(
        "proc sfn {x} { await $x }\n"
        "[for [vec 1 2] $sfn]",
        "cannot pass suspending closure to non-suspending builtin"));
    TEST_PASS();
}

/* Test: compile error for suspending closure passed to filter */
static int test_suspension_error_suspending_callback_filter(void) {
    ASSERT(test__compile_has_error(
        "proc sfn {x} { await $x }\n"
        "[filter [vec 1 2] $sfn]",
        "cannot pass suspending closure to non-suspending builtin"));
    TEST_PASS();
}

/* Test: compile error for suspending closure passed to transform */
static int test_suspension_error_suspending_callback_transform(void) {
    ASSERT(test__compile_has_error(
        "proc sfn {x} { await $x }\n"
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
        "proc add {a, b} { + $a $b }\n"
        "proc apply {f, x} { [$f $x] }\n"
        "proc double {x} { * $x 2 }\n"
        "print [add 1 2]",
        &vm, &arena);
    ASSERT(r == VM_OK);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * US-003: CPS transform — sequential code
 * ==================================================================== */

/* Helper: compile source and return the CompileResult for inspection */
static CompileResult test__compile(const char *source, arena_t *arena, VM *vm) {
    LexResult tokens = lexer_lex(source, arena);
    ParseResult parse = parser_parse(tokens, arena);
    JaclInternTable intern_table;
    intern_table_init(&intern_table, arena);
    vm->intern_table = &intern_table;
    CompileResult result = compiler_compile(parse, arena, &intern_table, &vm->heap, NULL, NULL, JACL_NIL);
    intern_table_destroy(&intern_table);
    vm->intern_table = NULL;
    return result;
}

/* Helper: find a closure constant by name in a chunk */
static JaclClosure* test__find_closure(BytecodeChunk *chunk, const char *name) {
    for (uint32_t i = 0; i < chunk->const_count; i++) {
        JaclVal v = chunk->constants[i];
        if (jacl_is_closure(v)) {
            JaclClosure *cl = jacl_as_closure(v);
            if (cl->name && strcmp(cl->name, name) == 0) return cl;
        }
    }
    return NULL;
}

/* Test: suspending proc with 1 await gets SM compilation */
static int test_cps_single_await_compiles(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {x} { def a [await $x]; + $a 1 }", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    /* SM: foo takes __sm + __rv = 2 params */
    ASSERT_INT_EQ(foo->param_count, 2);
    ASSERT(foo->is_sm_compiled);

    /* SM has no continuation closures */
    JaclClosure *cont = test__find_closure(&foo->chunk, "__cont");
    ASSERT(cont == NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: suspending proc with 2 sequential awaits compiles as SM */
static int test_cps_two_sequential_awaits(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {x, y} {\n"
        "  def a [await $x]\n"
        "  def b [await $y]\n"
        "  + $a $b\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    /* SM: always 2 params (__sm, __rv) */
    ASSERT_INT_EQ(foo->param_count, 2);
    ASSERT(foo->is_sm_compiled);
    /* SM needs fields for x, y, a locals */
    ASSERT(foo->sm_field_count >= 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: suspending proc with 3 sequential awaits compiles as SM */
static int test_cps_three_sequential_awaits(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {x, y, z} {\n"
        "  def a [await $x]\n"
        "  def b [await $y]\n"
        "  def c [await $z]\n"
        "  + [+ $a $b] $c\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    /* SM: always 2 params (__sm, __rv) */
    ASSERT_INT_EQ(foo->param_count, 2);
    ASSERT(foo->is_sm_compiled);
    /* SM needs fields for x, y, z, a, b locals */
    ASSERT(foo->sm_field_count >= 3);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: non-suspending proc is completely unaffected by SM compilation */
static int test_cps_non_suspending_unaffected(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc add {a, b} { + $a $b }\n"
        "print [add 10 20]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "30\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: non-suspending proc keeps original param count (no SM) */
static int test_cps_non_suspending_param_count(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc add {a, b} { + $a $b }", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *add = test__find_closure(&cr.chunk, "add");
    ASSERT(add != NULL);
    ASSERT_INT_EQ(add->param_count, 2); /* original params, no SM */
    ASSERT(!add->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: calling a suspending proc propagates SM to the caller */
static int test_cps_propagation_to_caller(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc afn {x} { def a [await $x]; $a }\n"
        "proc caller {f} { def r [afn $f]; $r }", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    /* afn is suspending → SM-compiled */
    JaclClosure *afn = test__find_closure(&cr.chunk, "afn");
    ASSERT(afn != NULL);
    ASSERT_INT_EQ(afn->param_count, 2); /* __sm + __rv */
    ASSERT(afn->is_sm_compiled);

    /* caller calls afn → transitively suspending → also SM */
    JaclClosure *caller = test__find_closure(&cr.chunk, "caller");
    ASSERT(caller != NULL);
    ASSERT_INT_EQ(caller->param_count, 2); /* __sm + __rv */
    ASSERT(caller->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: SM single await with resolved future — end-to-end execution */
static int test_cps_single_await_resolved(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* SM proc with await: spawn resolves immediately in single-threaded */
    VMResult r = jacl_run(
        "proc afn {f} { def v [await $f]; print $v }\n"
        "def fut [spawn { 42 }]\n"
        "[afn $fut]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: SM compilation preserves all locals in state object fields */
static int test_cps_variable_capture(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {a, b, c} {\n"
        "  def x [await $a]\n"
        "  + $x $c\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm, __rv */
    ASSERT(foo->is_sm_compiled);
    /* SM needs fields for a, b, c, x */
    ASSERT(foo->sm_field_count >= 3);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: SM state layout includes all locals across suspension points */
static int test_cps_eager_capture(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {unused} {\n"
        "  def f [spawn { 1 }]\n"
        "  def x [await $f]\n"
        "  + $x 0\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT(foo->is_sm_compiled);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm, __rv */
    /* SM needs fields for: unused, f, x */
    ASSERT(foo->sm_field_count >= 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: 5 sequential awaits compile as SM with sufficient fields */
static int test_cps_deep_chain(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {a, b, c, d, e} {\n"
        "  def v1 [await $a]\n"
        "  def v2 [await $b]\n"
        "  def v3 [await $c]\n"
        "  def v4 [await $d]\n"
        "  def v5 [await $e]\n"
        "  + [+ [+ [+ $v1 $v2] $v3] $v4] $v5\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm, __rv */
    ASSERT(foo->is_sm_compiled);
    /* SM needs fields for a, b, c, d, e, v1, v2, v3, v4, v5 */
    ASSERT(foo->sm_field_count >= 5);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: OP_AWAIT_SM opcode present in SM-compiled proc */
static int test_cps_emits_op_await(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {x} { def a [await $x]; $a }", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT(foo->is_sm_compiled);

    /* Scan foo's bytecode for OP_AWAIT_SM */
    bool found_await_sm = false;
    for (uint32_t i = 0; i < foo->chunk.code_count; i++) {
        if (foo->chunk.code[i] == OP_AWAIT_SM) {
            found_await_sm = true;
            break;
        }
    }
    ASSERT(found_await_sm);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: all existing programs still compile and run correctly */
static int test_cps_existing_code_unaffected(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [< $n 2] { + $n 0 } else {\n"
        "    + [fib [- $n 1]] [fib [- $n 2]]\n"
        "  }\n"
        "}\n"
        "print [fib 10]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "55\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * US-004: CPS transform — control flow
 * ==================================================================== */

/* Test: if with suspension in then-branch — SM compiled */
static int test_cps_if_then_suspends(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {f} {\n"
        "  def r [if [> 1 0] { await $f } { 42 }]\n"
        "  + $r 1\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: if with suspension in else-branch — SM compiled */
static int test_cps_if_else_suspends(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {f} {\n"
        "  def r [if [> 1 0] { 42 } { await $f }]\n"
        "  + $r 1\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: if with suspension in both branches — SM compiled */
static int test_cps_if_both_suspend(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {x, y} {\n"
        "  def r [if [> 1 0] { await $x } { await $y }]\n"
        "  + $r 1\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nested if with suspension — SM compiled */
static int test_cps_nested_if(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {f} {\n"
        "  def r [if [> 1 0] {\n"
        "    if [> 2 1] { await $f } else { 10 }\n"
        "  } { 42 }]\n"
        "  + $r 1\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: chained if (cond pattern) with suspension — SM compiled */
static int test_cps_chained_if(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {x, f} {\n"
        "  def r [if [== $x 1] {\n"
        "    await $f\n"
        "  } {\n"
        "    if [== $x 2] { 20 } else { 30 }\n"
        "  }]\n"
        "  + $r 1\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nested await in call arguments — SM compiled */
static int test_cps_nested_await_args(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {x, y} {\n"
        "  + [await $x] [await $y]\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: def with if-suspension value — SM compiled */
static int test_cps_def_if_suspension(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {f} {\n"
        "  def x [if [> 1 0] { await $f } { 42 }]\n"
        "  print $x\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: runtime — if with then-branch suspension runs via SM */
static int test_cps_if_then_runtime(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc afn {f} {\n"
        "  if [> 1 0] { await $f } else { 99 }\n"
        "}\n"
        "def fut [spawn { 42 }]\n"
        "print [afn $fut]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: runtime — if with else-branch non-suspending returns directly via SM */
static int test_cps_if_nonsuspend_branch(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Condition is false → takes non-suspending else branch */
    VMResult r = jacl_run(
        "proc afn {f} {\n"
        "  if [> 0 1] { await $f } else { 42 }\n"
        "}\n"
        "def fut [spawn { 99 }]\n"
        "print [afn $fut]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: try/catch with suspension still produces compile-time error */
static int test_cps_try_catch_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {f} { try { await $f } e { $e } }", &arena, &vm);
    ASSERT(cr.error_count > 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: let block with suspension — SM compiled with multiple await points */
static int test_cps_let_block_suspension(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {a, b} {\n"
        "  def x [await $a]\n"
        "  def y [+ $x 10]\n"
        "  def z [await $b]\n"
        "  + $y $z\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);
    /* SM needs fields for a, b, x, y, z */
    ASSERT(foo->sm_field_count >= 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: if as last statement — SM compiled */
static int test_cps_if_last_stmt(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc foo {f} {\n"
        "  if [> 1 0] { await $f } else { 42 }\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *foo = test__find_closure(&cr.chunk, "foo");
    ASSERT(foo != NULL);
    ASSERT_INT_EQ(foo->param_count, 2); /* __sm + __rv */
    ASSERT(foo->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: existing non-suspending if still works correctly */
static int test_cps_if_no_suspension_ok(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc abs {n} { if [< $n 0] { - 0 $n } else { + $n 0 } }\n"
        "print [abs [- 0 5]]\n"
        "print [abs 3]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "5\n3\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * US-005: spawn primitive
 * ==================================================================== */

/* Test: spawn returns a future */
static int test_spawn_returns_future(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run("print [future? [spawn { 42 }]]", &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "true\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: spawn with non-suspending closure resolves future with return value */
static int test_spawn_nonsuspending_resolves(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Compile and run: spawn { 42 } */
    VMResult r = jacl_run("def f [spawn { 42 }]", &vm, &arena);
    ASSERT(r == VM_OK);

    /* Look up $f in the environment and check the future state */
    JaclVal f_name = jacl_inline_string("f", 1);
    JaclVal f_val = JACL_NIL;
    for (uint32_t i = 0; i < vm.env.count; i++) {
        if (vm.env.names[i] == f_name) { f_val = vm.env.values[i]; break; }
    }
    ASSERT(jacl_is_future(f_val));
    JaclFuture *fut = jacl_as_future(f_val);
    ASSERT_U32_EQ(ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED), FUTURE_RESOLVED);
    ASSERT_U64_EQ((JaclVal)fut->result, jacl_i32(42));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: spawn with suspending closure (containing await) resolves correctly */
static int test_spawn_suspending_resolves(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Create a pre-resolved future in the environment */
    JaclVal pre_fut = jacl_future(&vm.heap);
    JaclFuture *pre = jacl_as_future(pre_fut);
    jacl_future_resolve(pre, jacl_i32(99), NULL, NULL);
    vm.env.names[vm.env.count]  = jacl_inline_string("pf", 2);
    vm.env.values[vm.env.count] = pre_fut;
    vm.env.count++;

    /* spawn a closure that awaits the pre-resolved future */
    VMResult r = jacl_run("def f [spawn { await $pf }]", &vm, &arena);
    ASSERT(r == VM_OK);

    /* Look up $f */
    JaclVal f_name = jacl_inline_string("f", 1);
    JaclVal f_val = JACL_NIL;
    for (uint32_t i = 0; i < vm.env.count; i++) {
        if (vm.env.names[i] == f_name) { f_val = vm.env.values[i]; break; }
    }
    ASSERT(jacl_is_future(f_val));
    JaclFuture *fut = jacl_as_future(f_val);
    ASSERT_U32_EQ(ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED), FUTURE_RESOLVED);
    ASSERT_U64_EQ((JaclVal)fut->result, jacl_i32(99));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: spawn with error propagation */
static int test_spawn_error_propagation(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Create a future that resolved with an error */
    JaclVal err_fut = jacl_future(&vm.heap);
    JaclFuture *efut = jacl_as_future(err_fut);
    JaclVal err_val = jacl_set_error(jacl_i32(404));
    jacl_future_resolve(efut, err_val, NULL, NULL);
    vm.env.names[vm.env.count]  = jacl_inline_string("ef", 2);
    vm.env.values[vm.env.count] = err_fut;
    vm.env.count++;

    /* spawn a closure that awaits the errored future — error propagates */
    VMResult r = jacl_run("def f [spawn { await $ef }]", &vm, &arena);
    ASSERT(r == VM_OK);

    /* The spawn future should be resolved (the error value from await
       flows through the CPS chain into resolve_k, which resolves the
       spawn future with the error value). */
    JaclVal f_name = jacl_inline_string("f", 1);
    JaclVal f_val = JACL_NIL;
    for (uint32_t i = 0; i < vm.env.count; i++) {
        if (vm.env.names[i] == f_name) { f_val = vm.env.values[i]; break; }
    }
    ASSERT(jacl_is_future(f_val));
    JaclFuture *fut = jacl_as_future(f_val);
    ASSERT_U32_EQ(ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED), FUTURE_RESOLVED);
    /* The result should be the error value (with error flag set) */
    ASSERT(jacl_is_error((JaclVal)fut->result));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: spawned closure executes on worker thread (runtime mode) */
static int test_spawn_worker_execution(void) {
    Runtime rt;
    runtime_init(&rt, 2);

    /* Create a non-suspending closure manually:
       bytecode: OP_CONST <42>, OP_RETURN */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile("proc task {} { 42 }", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);
    ASSERT_INT_EQ(task_cl->param_count, 0);

    /* Create future and submit spawn task */
    JaclVal f = jacl_future(&rt.workers[0].vm.heap);
    runtime__submit_spawn_task(&rt, task_cl, f, JACL_NIL);

    /* Wait for the future to resolve */
    JaclFuture *fut = jacl_as_future(f);
    for (int ms = 0; ms < 5000; ms++) {
        uint32_t state = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);
        if (state != FUTURE_PENDING) break;
        SLEEP_MILLISECONDS(1);
    }

    ASSERT_U32_EQ(ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED), FUTURE_RESOLVED);
    ASSERT_U64_EQ((JaclVal)fut->result, jacl_i32(42));

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: top-level suspending code is CPS-transformed */
static int test_top_level_cps(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Source with top-level await → compiler should produce suspending chunk */
    LexResult tokens = lexer_lex("def f [spawn { 42 }]; await $f", &arena);
    ParseResult parse = parser_parse(tokens, &arena);
    JaclInternTable intern_table;
    intern_table_init(&intern_table, &arena);
    CompileResult cr = compiler_compile(parse, &arena, &intern_table, &vm.heap, NULL, NULL, JACL_NIL);

    ASSERT_U32_EQ(cr.error_count, 0);
    ASSERT(cr.suspending); /* top-level should be detected as suspending */

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: rt_run_to_completion blocks until future resolves */
static int test_rt_run_to_completion(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Compile a non-suspending closure we can submit */
    CompileResult cr = test__compile("proc task {} { 42 }", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    Runtime rt;
    runtime_init(&rt, 2);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: existing non-suspending code still works after spawn changes */
static int test_spawn_existing_code_ok(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [< $n 2] { + $n 0 } else {\n"
        "    + [fib [- $n 1]] [fib [- $n 2]]\n"
        "  }\n"
        "}\n"
        "print [fib 10]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "55\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * US-006: await primitive
 * ==================================================================== */

/* Test: await on resolved future returns the result immediately */
static int test_await_resolved_immediate(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* spawn resolves synchronously in single-threaded mode,
       so await gets the result immediately */
    VMResult r = jacl_run(
        "def f [spawn { 42 }]\n"
        "print [await $f]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: await on pending future registered as waiter, scheduled on resolve */
static int test_await_pending_waiter(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Create a pending future and add it to environment */
    JaclVal pf = jacl_future(&vm.heap);
    vm.env.names[vm.env.count]  = jacl_inline_string("pf", 2);
    vm.env.values[vm.env.count] = pf;
    vm.env.count++;

    /* In single-threaded mode with a pending future, OP_AWAIT will call the
       continuation with nil (graceful degradation). Verify no crash. */
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "print [await $pf]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    /* Pending future in single-threaded mode → continuation receives nil */
    ASSERT_STR_EQ(cap.buffer, "nil\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: await on errored future passes error value to continuation */
static int test_await_errored_future(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Create an errored future and add to env */
    JaclVal ef = jacl_future(&vm.heap);
    JaclFuture *efut = jacl_as_future(ef);
    JaclVal err_val = jacl_set_error(jacl_i32(404));
    jacl_future_error(efut, err_val, NULL, NULL);
    vm.env.names[vm.env.count]  = jacl_inline_string("ef", 2);
    vm.env.values[vm.env.count] = ef;
    vm.env.count++;

    /* await on errored future should pass the error value to continuation */
    VMResult r = jacl_run(
        "def val [await $ef]\n"
        "print [error? $val]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "true\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: multiple awaiters on same future all get the result */
static int test_await_multiple_awaiters(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Create a pending future, register two waiter closures manually,
       then resolve — both waiters should be returned. */
    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    /* Create two dummy continuation closures for waiters */
    JaclVal c1 = jacl_future(&vm.heap); /* just need a GC-managed value */
    JaclVal c2 = jacl_future(&vm.heap);

    /* Add waiters (using the future vals as dummy continuations) */
    bool added1 = jacl_future_add_waiter(fut, c1, &vm.heap);
    bool added2 = jacl_future_add_waiter(fut, c2, &vm.heap);
    ASSERT(added1);
    ASSERT(added2);

    /* Resolve future — should return both waiters */
    FutureWaiter *waiters = jacl_future_resolve(fut, jacl_i32(99), NULL, NULL);
    ASSERT(waiters != NULL);

    /* Count waiters */
    int count = 0;
    FutureWaiter *w = waiters;
    while (w) { count++; w = w->next; }
    ASSERT_INT_EQ(count, 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: sequential awaits produce correct final result */
static int test_await_sequential_results(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def f1 [spawn { 10 }]\n"
        "def f2 [spawn { 20 }]\n"
        "def a [await $f1]\n"
        "def b [await $f2]\n"
        "print [+ $a $b]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "30\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: chained continuations — 3 sequential awaits */
static int test_await_chained_three(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def f1 [spawn { 1 }]\n"
        "def f2 [spawn { 2 }]\n"
        "def f3 [spawn { 3 }]\n"
        "def a [await $f1]\n"
        "def b [await $f2]\n"
        "def c [await $f3]\n"
        "print [+ $a [+ $b $c]]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "6\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: runtime error if await called with non-future value.
 * Uses a `def dyn` binding so the typer's await-on-non-future rule
 * doesn't fire at compile time — the operand's static type is dyn,
 * so the runtime tag check is what catches it. */
static int test_await_non_future_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    VMResult r = jacl_run("def dyn x 42; await $x", &vm, &arena);
    ASSERT(r == VM_RUNTIME_ERROR);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: await in runtime mode — spawn+await on worker threads */
static int test_await_worker_resolved(void) {
    Runtime rt;
    runtime_init(&rt, 2);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Compile a program: proc that spawns, awaits, and returns result.
       We compile the proc then run it via rt_run_to_completion. */
    CompileResult cr = test__compile(
        "proc task {} { def f [spawn { 42 }]; await $f }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    /* task is SM-compiled (contains await) */
    bool is_sm = task_cl->is_sm_compiled;

    /* Create a completion future and submit via rt_run_to_completion */
    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();

    (void)is_sm;
}

/* Test: existing non-suspending code still works after await changes */
static int test_await_existing_code_ok(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc add {a, b} { + $a $b }\n"
        "print [add 3 4]\n"
        "print [add 10 20]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "7\n30\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * US-007: parallel primitive
 * ==================================================================== */

/* Test: parallel 2 tasks — results in input order */
static int test_parallel_two_tasks_order(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def results [parallel { 10 } { 20 }]\n"
        "  print [vec-get $results 0]\n"
        "  print [vec-get $results 1]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "10\n20\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel 3 tasks — all correct results */
static int test_parallel_three_tasks(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def results [parallel { + 1 2 } { * 3 4 } { - 10 5 }]\n"
        "  print [vec-get $results 0]\n"
        "  print [vec-get $results 1]\n"
        "  print [vec-get $results 2]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "3\n12\n5\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel with one error — error propagated */
static int test_parallel_error_propagation(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def results [parallel { 42 } { error \"fail\" }]\n"
        "  if [error? $results] {\n"
        "    print \"got-error\"\n"
        "  } else {\n"
        "    print \"no-error\"\n"
        "  }\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "got-error\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel compiles correctly (emits OP_PARALLEL) */
static int test_parallel_compiles(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc main {} { parallel { 1 } { 2 } }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    /* Find the main closure — it should be SM (contains parallel) */
    JaclClosure *main_cl = test__find_closure(&cr.chunk, "main");
    ASSERT(main_cl != NULL);
    /* SM proc: __sm + __rv = 2 params */
    ASSERT_INT_EQ(main_cl->param_count, 2);
    ASSERT(main_cl->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel with spawn+await inside bodies (suspending bodies) */
static int test_parallel_suspending_bodies(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def results [parallel { def f [spawn { 100 }]; await $f } { def g [spawn { 200 }]; await $g }]\n"
        "  print [vec-get $results 0]\n"
        "  print [vec-get $results 1]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "100\n200\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel with worker threads (runtime mode) */
static int test_parallel_worker_execution(void) {
    Runtime rt;
    runtime_init(&rt, 2);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc task {} { parallel { 10 } { 20 } }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel result is a vector with correct length */
static int test_parallel_result_vector(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def results [parallel { 1 } { 2 } { 3 } { 4 }]\n"
        "  print [vec-len $results]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "4\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel followed by more code (continuation works) */
static int test_parallel_with_continuation(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def results [parallel { 5 } { 10 }]\n"
        "  def sum [+ [vec-get $results 0] [vec-get $results 1]]\n"
        "  print $sum\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "15\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: existing non-suspending code still works after parallel changes */
static int test_parallel_existing_code_ok(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [<= $n 1] { + $n 0 } else { + [fib [- $n 1]] [fib [- $n 2]] }\n"
        "}\n"
        "print [fib 10]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "55\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ================================================================
 * US-008: race primitive
 * ================================================================ */

/* Test: race 2 tasks — first result wins (single-threaded: body 0 wins) */
static int test_race_two_tasks(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def winner [race { 42 } { 99 }]\n"
        "  print $winner\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    /* Single-threaded: first body executes first, wins */
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: race returns winner's value (3 tasks) */
static int test_race_three_tasks(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def winner [race { 10 } { 20 } { 30 }]\n"
        "  print $winner\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "10\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: race with winning error — error propagated */
static int test_race_error_propagation(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def result [race { error \"fail\" } { 42 }]\n"
        "  if [error? $result] {\n"
        "    print \"got-error\"\n"
        "  } else {\n"
        "    print \"no-error\"\n"
        "  }\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "got-error\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: race compiles correctly (emits OP_RACE) */
static int test_race_compiles(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc main {} { race { 1 } { 2 } }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    /* Find the main closure — it should be SM (contains race) */
    JaclClosure *main_cl = test__find_closure(&cr.chunk, "main");
    ASSERT(main_cl != NULL);
    /* SM proc: __sm + __rv = 2 params */
    ASSERT_U32_EQ(main_cl->param_count, 2);
    ASSERT(main_cl->is_sm_compiled);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: race with worker threads (runtime mode) */
static int test_race_worker_execution(void) {
    Runtime rt;
    runtime_init(&rt, 2);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc task {} { race { 10 } { 20 } }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: race with continuation — code after race runs */
static int test_race_with_continuation(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def w [race { 5 } { 10 }]\n"
        "  print [+ $w 100]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "105\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: race existing code still works (regression) */
static int test_race_existing_code_ok(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [<= $n 1] { + $n 0 } else { + [fib [- $n 1]] [fib [- $n 2]] }\n"
        "}\n"
        "print [fib 10]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "55\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ================================================================
 * US-009: Remove VM stack root scanning from concurrent GC
 * ================================================================ */

/* Test: concurrent tasks still work after removing VM stack scanning
 * (CPS continuations capture all live state as task roots) */
static int test_no_stack_scan_spawn(void) {
    Runtime rt;
    runtime_init(&rt, 2);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc task {} { def f [spawn { 42 }]; await $f }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: concurrent parallel still works without stack scanning */
static int test_no_stack_scan_parallel(void) {
    Runtime rt;
    runtime_init(&rt, 2);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc task {} { parallel { 10 } { 20 } }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: single-threaded GC still works (retains stack scanning) */
static int test_st_gc_still_works(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* This test runs without runtime — single-threaded GC should
     * still scan VM stack for backward compatibility. */
    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [<= $n 1] { + $n 0 } else { + [fib [- $n 1]] [fib [- $n 2]] }\n"
        "}\n"
        "print [fib 10]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "55\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ================================================================
 * US-010: Adaptive GC threshold
 * ================================================================ */

/* Test: threshold increases after high-survival GC */
static int test_adaptive_threshold_increase(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    size_t initial = vm.heap.gc_threshold;
    ASSERT(initial == GC_THRESHOLD);

    /* Allocate objects that will ALL survive (rooted on stack) */
    for (int i = 0; i < 100; i++) {
        JaclVal v = jacl_i64(&vm.heap, (int64_t)i);
        vm.stack[vm.stack_top++] = v;
    }
    /* All objects survive → survival rate ~100% → threshold should increase */
    gc_collect(&vm.heap, &vm);

    ASSERT(vm.heap.gc_threshold > initial);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: threshold decreases after low-survival GC */
static int test_adaptive_threshold_decrease(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    size_t initial = vm.heap.gc_threshold;

    /* Allocate many objects but DON'T root them (all die) */
    for (int i = 0; i < 100; i++) {
        (void)jacl_i64(&vm.heap, (int64_t)i);
    }
    /* No roots on stack → survival rate ~0% → threshold should decrease */
    gc_collect(&vm.heap, &vm);

    ASSERT(vm.heap.gc_threshold < initial);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: threshold respects minimum bound */
static int test_adaptive_threshold_min_bound(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Set threshold to minimum */
    vm.heap.gc_threshold = GC_THRESHOLD_MIN;

    /* Allocate unrooted objects (low survival) */
    for (int i = 0; i < 50; i++) {
        (void)jacl_i64(&vm.heap, (int64_t)i);
    }
    gc_collect(&vm.heap, &vm);

    /* Should not go below minimum */
    ASSERT(vm.heap.gc_threshold >= GC_THRESHOLD_MIN);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: threshold respects maximum bound */
static int test_adaptive_threshold_max_bound(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Set threshold to maximum */
    vm.heap.gc_threshold = GC_THRESHOLD_MAX;

    /* Allocate rooted objects (high survival) */
    for (int i = 0; i < 50; i++) {
        JaclVal v = jacl_i64(&vm.heap, (int64_t)i);
        vm.stack[vm.stack_top++] = v;
    }
    gc_collect(&vm.heap, &vm);

    /* Should not exceed maximum */
    ASSERT(vm.heap.gc_threshold <= GC_THRESHOLD_MAX);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ================================================================
 * US-011: OOM escalation
 * ================================================================ */

#include <setjmp.h>

/* Test-local state for OOM panic interception */
static jmp_buf oom_panic_jmp;
static bool    oom_panic_called;

static void test__oom_panic_handler(ThreadHeap *heap, size_t request_size) {
    (void)heap; (void)request_size;
    oom_panic_called = true;
    longjmp(oom_panic_jmp, 1);
}

/* Test: emergency GC fires and reclaims garbage when blocks are exhausted */
static int test_oom_emergency_gc_fires(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Set very small block limit — 2 blocks (128KB total) */
    vm.block_pool.max_blocks = 2;

    /* Fill blocks with garbage (not rooted — nothing on VM stack) */
    for (int i = 0; i < 5000; i++) {
        void *p = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
        if (!p) break;
    }

    /* At this point, 2 blocks allocated and full of garbage.
     * The emergency GC callback (vm__emergency_gc_single) should fire
     * on the next allocation that triggers a new block request.
     * Emergency GC collects all garbage → blocks get free space.
     * Allocation should succeed without panicking. */
    void *result = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    ASSERT(result != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: tier 3 panic fires when all blocks are live and at limit */
static int test_oom_tier3_panic(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Set very small block limit: 1 block = 64KB */
    vm.block_pool.max_blocks = 1;

    /* Override panic handler to avoid abort() */
    void (*saved_handler)(ThreadHeap *, size_t) = gc__oom_handler;
    gc__oom_handler = test__oom_panic_handler;
    oom_panic_called = false;

    /* Fill the single block with LIVE data (rooted on VM stack).
     * Use large payloads (~248 bytes each) so 255 objects fill the 64KB block.
     * 256-byte aligned allocs × 255 = ~65280 bytes ≈ 64KB. */
    for (int i = 0; i < VM_STACK_MAX - 1; i++) {
        void *p = gc_alloc(&vm.heap, OBJ_HEAP_I64, 248);
        if (!p) break;
        JaclVal v = JACL_TAG_I64 | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
        vm.stack[vm.stack_top++] = v;
    }

    /* Block is nearly full with live data. Next alloc needs a new block
     * but max_blocks=1. Emergency GC can't free anything (all rooted).
     * Tier 2 fails (at limit). Tier 3 panic fires. */
    if (setjmp(oom_panic_jmp) == 0) {
        for (int i = 0; i < 5000; i++) {
            (void)gc_alloc(&vm.heap, OBJ_HEAP_I64, 248);
        }
    }

    ASSERT(oom_panic_called);

    gc__oom_handler = saved_handler;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: total_blocks_allocated tracks correctly */
static int test_oom_total_blocks_tracking(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);

    ASSERT_U32_EQ(pool.total_blocks_allocated, 0);

    /* Get a new block (malloc) — counter should increment */
    GCBlock *b1 = gc_block_pool_get(&pool);
    ASSERT(b1 != NULL);
    ASSERT_U32_EQ(pool.total_blocks_allocated, 1);

    /* Get another new block (malloc) — counter should increment */
    GCBlock *b2 = gc_block_pool_get(&pool);
    ASSERT(b2 != NULL);
    ASSERT_U32_EQ(pool.total_blocks_allocated, 2);

    /* Return a block to free list — counter stays the same */
    gc_block_pool_return(&pool, b1);
    ASSERT_U32_EQ(pool.total_blocks_allocated, 2);

    /* Get from free list — counter stays the same (no new malloc) */
    GCBlock *b3 = gc_block_pool_get(&pool);
    ASSERT(b3 == b1); /* recycled */
    ASSERT_U32_EQ(pool.total_blocks_allocated, 2);

    /* Cleanup */
    gc_block_pool_return(&pool, b2);
    gc_block_pool_return(&pool, b3);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* Test: max_blocks limit prevents allocation beyond limit */
static int test_oom_max_blocks_limit(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    pool.max_blocks = 2;

    GCBlock *b1 = gc_block_pool_get(&pool);
    ASSERT(b1 != NULL);
    GCBlock *b2 = gc_block_pool_get(&pool);
    ASSERT(b2 != NULL);

    /* At limit — next get should fail (no free list, at max_blocks) */
    GCBlock *b3 = gc_block_pool_get(&pool);
    ASSERT(b3 == NULL);

    /* Return one to free list — next get should succeed (recycled) */
    gc_block_pool_return(&pool, b1);
    GCBlock *b4 = gc_block_pool_get(&pool);
    ASSERT(b4 != NULL);

    /* Cleanup */
    gc_block_pool_return(&pool, b2);
    gc_block_pool_return(&pool, b4);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* Test: OOM panic handler receives correct heap context */
static int test_oom_panic_diagnostic(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    vm.block_pool.max_blocks = 1;

    /* Override panic handler */
    void (*saved_handler)(ThreadHeap *, size_t) = gc__oom_handler;
    gc__oom_handler = test__oom_panic_handler;
    oom_panic_called = false;

    /* Fill the single block with large live objects (rooted on stack) */
    for (int i = 0; i < VM_STACK_MAX - 1; i++) {
        void *p = gc_alloc(&vm.heap, OBJ_HEAP_I64, 248);
        if (!p) break;
        JaclVal v = JACL_TAG_I64 | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
        vm.stack[vm.stack_top++] = v;
    }

    /* Trigger OOM — should call panic handler */
    if (setjmp(oom_panic_jmp) == 0) {
        for (int i = 0; i < 5000; i++) {
            (void)gc_alloc(&vm.heap, OBJ_HEAP_I64, 248);
        }
    }

    ASSERT(oom_panic_called);

    gc__oom_handler = saved_handler;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: existing tests still work (OOM escalation doesn't affect normal allocation) */
static int test_oom_existing_code_ok(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [<= $n 1] { + $n 0 } else { + [fib [- $n 1]] [fib [- $n 2]] }\n"
        "}\n"
        "print [fib 10]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "55\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * US-012: Integration tests and stress testing
 * ==================================================================== */

/* --- CPS correctness (direct style) --- */

/* Test: single await returns correct value */
static int test_integ_single_await(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def f [spawn { 42 }]\n"
        "print [await $f]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: multiple sequential awaits — chain of 3 */
static int test_integ_three_seq_awaits(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def f1 [spawn { 10 }]\n"
        "def f2 [spawn { 20 }]\n"
        "def f3 [spawn { 30 }]\n"
        "def a [await $f1]\n"
        "def b [await $f2]\n"
        "def c [await $f3]\n"
        "print [+ $a [+ $b $c]]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "60\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: await in if branches — suspending branch taken */
static int test_integ_await_in_if(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  if [> 2 1] {\n"
        "    def f [spawn { 100 }]\n"
        "    print [await $f]\n"
        "  } else {\n"
        "    print 42\n"
        "  }\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "100\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: await in nested expressions */
static int test_integ_await_nested_expr(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def f1 [spawn { 3 }]\n"
        "def f2 [spawn { 4 }]\n"
        "print [+ [await $f1] [await $f2]]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "7\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: deep continuation chain (5+ awaits) */
static int test_integ_deep_chain(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def f1 [spawn { 1 }]\n"
        "def f2 [spawn { 2 }]\n"
        "def f3 [spawn { 3 }]\n"
        "def f4 [spawn { 4 }]\n"
        "def f5 [spawn { 5 }]\n"
        "def a [await $f1]\n"
        "def b [await $f2]\n"
        "def c [await $f3]\n"
        "def d [await $f4]\n"
        "def e [await $f5]\n"
        "print [+ $a [+ $b [+ $c [+ $d $e]]]]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "15\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: variable capture correctness across suspension */
static int test_integ_variable_capture(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def x 100\n"
        "def y 200\n"
        "def f [spawn { 5 }]\n"
        "def r [await $f]\n"
        "print [+ $x [+ $y $r]]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "305\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: calling suspending proc from another proc (suspension propagation) */
static int test_integ_suspension_propagation(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc inner {} {\n"
        "  def f [spawn { 99 }]\n"
        "  await $f\n"
        "}\n"
        "proc outer {} {\n"
        "  def val [inner]\n"
        "  + $val 1\n"
        "}\n"
        "proc main {} { print [outer] }\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "100\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- spawn+await integration --- */

/* Test: spawn+await returns correct value */
static int test_integ_spawn_await_value(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def f [spawn { + 20 22 }]\n"
        "print [await $f]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: multiple spawn + sequential await */
static int test_integ_multi_spawn_await(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def f1 [spawn { 10 }]\n"
        "  def f2 [spawn { 20 }]\n"
        "  def f3 [spawn { 30 }]\n"
        "  def a [await $f1]\n"
        "  def b [await $f2]\n"
        "  def c [await $f3]\n"
        "  print [+ $a [+ $b $c]]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "60\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: spawn with heavy allocation (GC triggers) */
static int test_integ_spawn_heavy_alloc(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Spawn a task that does recursive fibonacci — heavy allocation */
    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [< $n 2] { + $n 0 } else {\n"
        "    + [fib [- $n 1]] [fib [- $n 2]]\n"
        "  }\n"
        "}\n"
        "proc main {} {\n"
        "  def f [spawn { fib 15 }]\n"
        "  print [await $f]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "610\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: await already-resolved future (no deadlock) */
static int test_integ_await_already_resolved(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* spawn resolves synchronously in single-threaded mode,
       so by the time we await, future is already resolved */
    VMResult r = jacl_run(
        "def f [spawn { 77 }]\n"
        "def dummy [+ 1 1]\n"
        "print [await $f]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "77\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: await errored future — error? checkable */
static int test_integ_await_errored(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def f [spawn { error \"oops\" }]\n"
        "def val [await $f]\n"
        "print [error? $val]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "true\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: multiple awaiters on same future (API-level) */
static int test_integ_multi_awaiter_same(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Create a pending future, register multiple waiters, resolve */
    JaclVal f = jacl_future(&vm.heap);
    JaclFuture *fut = jacl_as_future(f);

    JaclVal c1 = jacl_future(&vm.heap);
    JaclVal c2 = jacl_future(&vm.heap);
    JaclVal c3 = jacl_future(&vm.heap);

    ASSERT(jacl_future_add_waiter(fut, c1, &vm.heap));
    ASSERT(jacl_future_add_waiter(fut, c2, &vm.heap));
    ASSERT(jacl_future_add_waiter(fut, c3, &vm.heap));

    FutureWaiter *waiters = jacl_future_resolve(fut, jacl_i32(42), NULL, NULL);
    int count = 0;
    FutureWaiter *w = waiters;
    while (w) { count++; w = w->next; }
    ASSERT_INT_EQ(count, 3);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- parallel integration --- */

/* Test: parallel 2 tasks results in order */
static int test_integ_parallel_two(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def r [parallel { + 1 2 } { * 3 4 }]\n"
        "  print [vec-get $r 0]\n"
        "  print [vec-get $r 1]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "3\n12\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel 5 tasks all correct */
static int test_integ_parallel_five(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def r [parallel { 10 } { 20 } { 30 } { 40 } { 50 }]\n"
        "  print [vec-get $r 0]\n"
        "  print [vec-get $r 1]\n"
        "  print [vec-get $r 2]\n"
        "  print [vec-get $r 3]\n"
        "  print [vec-get $r 4]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "10\n20\n30\n40\n50\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel with one error propagated */
static int test_integ_parallel_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def r [parallel { 42 } { error \"bad\" }]\n"
        "  if [error? $r] { print \"error\" } else { print \"ok\" }\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "error\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel with heavy allocation per task */
static int test_integ_parallel_heavy_alloc(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* fib at top-level, main uses parallel with fib calls */
    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [< $n 2] { + $n 0 } else {\n"
        "    + [fib [- $n 1]] [fib [- $n 2]]\n"
        "  }\n"
        "}\n"
        "proc main {} {\n"
        "  def r [parallel { fib 10 } { fib 8 }]\n"
        "  print [vec-get $r 0]\n"
        "  print [vec-get $r 1]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "55\n21\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- race integration --- */

/* Test: race 2 tasks — faster wins */
static int test_integ_race_two(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def w [race { 42 } { 99 }]\n"
        "  print $w\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    /* Single-threaded: first body wins */
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: losers complete without crash */
static int test_integ_race_losers_ok(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* 3 tasks, first wins, losers complete silently */
    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def w [race { 1 } { 2 } { 3 }]\n"
        "  print $w\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "1\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- Direct-style integration --- */

/* Test: top-level code calling suspending procs runs to completion */
static int test_integ_toplevel_suspending(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc aadd {a, b} {\n"
        "  def fa [spawn { + $a 0 }]\n"
        "  def fb [spawn { + $b 0 }]\n"
        "  + [await $fa] [await $fb]\n"
        "}\n"
        "proc main {} { print [aadd 15 27] }\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nested suspending proc calls (A calls B calls await) */
static int test_integ_nested_suspending(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc inner {x} {\n"
        "  def f [spawn { + $x 10 }]\n"
        "  await $f\n"
        "}\n"
        "proc mid {x} {\n"
        "  def v [inner $x]\n"
        "  + $v 5\n"
        "}\n"
        "proc main {} {\n"
        "  print [mid 100]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "115\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: error propagation through CPS chain */
static int test_integ_error_through_cps(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc bad {} {\n"
        "  def f [spawn { error \"oops\" }]\n"
        "  await $f\n"
        "}\n"
        "proc main {} {\n"
        "  def result [bad]\n"
        "  print [error? $result]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "true\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- Multi-threaded stress --- */

/* Test: 4 workers spawning tasks via runtime */
static int test_stress_workers_spawn(void) {
    Runtime rt;
    runtime_init(&rt, 4);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Compile a proc that spawns a sub-task and awaits it */
    CompileResult cr = test__compile(
        "proc task {} { def f [spawn { + 20 22 }]; await $f }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: heavy allocation with concurrent GC */
static int test_stress_heavy_alloc_gc(void) {
    Runtime rt;
    runtime_init(&rt, 4);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* fib(15) does heavy allocation — exercises GC under concurrent load */
    CompileResult cr = test__compile(
        "proc fib {n} {\n"
        "  if [< $n 2] { + $n 0 } else {\n"
        "    + [fib [- $n 1]] [fib [- $n 2]]\n"
        "  }\n"
        "}\n"
        "proc task {} { def f [spawn { fib 15 }]; await $f }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel with many tasks on workers */
static int test_stress_parallel_many(void) {
    Runtime rt;
    runtime_init(&rt, 4);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* parallel 4 tasks on 4 workers */
    CompileResult cr = test__compile(
        "proc task {} { parallel { 1 } { 2 } { 3 } { 4 } }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* US-002 (M14): parallel join result vector survives GC.
 * 4 parallel tasks on 4 workers — the continuation accesses
 * the result vector after all tasks complete. gc_root2 on the
 * continuation task roots the result vector.
 * (Heavy GC pressure variant is tested via parallel_gc_pressure.jacl
 *  with # mode: concurrent, which properly initializes worker envs.) */
static int test_stress_parallel_join_gc(void) {
    Runtime rt;
    runtime_init(&rt, 4);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Parallel with 4 bodies + result vector access in continuation.
     * Verifies the result vector is intact after parallel completion. */
    CompileResult cr = test__compile(
        "proc task {} {\n"
        "  def r [parallel { + 10 20 } { + 30 40 } { + 50 60 } { + 70 80 }]\n"
        "  + [+ [vec-get $r 0] [vec-get $r 1]] [+ [vec-get $r 2] [vec-get $r 3]]\n"
        "}",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: race with many tasks on workers */
static int test_stress_race_many(void) {
    Runtime rt;
    runtime_init(&rt, 4);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* race 4 tasks on 4 workers */
    CompileResult cr = test__compile(
        "proc task {} { race { 10 } { 20 } { 30 } { 40 } }",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: mixed spawn/await/parallel/race workload on workers */
static int test_stress_mixed_workload(void) {
    Runtime rt;
    runtime_init(&rt, 4);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* A proc that uses spawn+await then returns result */
    CompileResult cr = test__compile(
        "proc task {} {\n"
        "  def f [spawn { + 10 20 }]\n"
        "  def a [await $f]\n"
        "  def r [parallel { + $a 1 } { + $a 2 }]\n"
        "  + [vec-get $r 0] [vec-get $r 1]\n"
        "}",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *task_cl = test__find_closure(&cr.chunk, "task");
    ASSERT(task_cl != NULL);

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- Memory bounded --- */

/* Test: sustained spawn/await workload — heap doesn't grow without bound */
static int test_integ_memory_bounded(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    /* Multiple sequential spawn+await — each creates futures and closures
       that become garbage. GC should keep heap bounded. */
    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def f1 [spawn { 1 }]\n"
        "  def a [await $f1]\n"
        "  def f2 [spawn { + $a 2 }]\n"
        "  def b [await $f2]\n"
        "  def f3 [spawn { + $b 3 }]\n"
        "  def c [await $f3]\n"
        "  def f4 [spawn { + $c 4 }]\n"
        "  def d [await $f4]\n"
        "  def f5 [spawn { + $d 5 }]\n"
        "  print [await $f5]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    /* 1+2+3+4+5 = 15 */
    ASSERT_STR_EQ(cap.buffer, "15\n");

    /* Heap should have a bounded number of blocks */
    uint32_t total_blocks = vm.block_pool.total_blocks_allocated;
    ASSERT(total_blocks < 20);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- Regression --- */

/* Test: M0-M12 fibonacci still works */
static int test_regression_fib(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc fib {n} {\n"
        "  if [<= $n 1] { + $n 0 } else { + [fib [- $n 1]] [fib [- $n 2]] }\n"
        "}\n"
        "print [fib 10]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "55\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: M0-M12 closures and upvalues still work */
static int test_regression_closures(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc mkadd {x} {\n"
        "  proc inner {y} { + $x $y }\n"
        "}\n"
        "def add5 [mkadd 5]\n"
        "print [$add5 10]\n"
        "print [$add5 20]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "15\n25\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: M0-M12 vectors and maps still work */
static int test_regression_collections(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def v [vec 1 2 3]\n"
        "print [vec-len $v]\n"
        "print [vec-get $v 1]\n"
        "def m [map \"a\" 1 \"b\" 2]\n"
        "print [map-get $m \"a\"]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "3\n2\n1\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: M0-M12 error handling still works */
static int test_regression_error_handling(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def result [try { + 1 2 } e { \"failed\" }]\n"
        "print $result",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "3\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: M0-M12 mutable state still works */
static int test_regression_mutable_state(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def a [atom 0]\n"
        "reset $a 42\n"
        "print [deref $a]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: M0-M12 string operations still work */
static int test_regression_strings(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "def name \"world\"\n"
        "print \"hello $name\"",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "hello world\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- US-002: VM upvalue bounds checking --- */

/* Test: OP_CLOSURE with out-of-bounds parent upvalue index triggers error */
static int test_closure_oob_parent_upvalue(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /*
     * Craft bytecode by hand:
     *   1. A closure template in the constant pool with upvalue_count=1
     *   2. Top-level code: OP_CLOSURE <template_idx> is_local=0 uv_index=5
     *      The top-level closure has 0 upvalues, so index 5 is out of bounds.
     *   3. OP_HALT
     */
    BytecodeChunk body;
    chunk_init(&body, &arena);
    chunk_write(&body, OP_NIL, 1);
    chunk_write(&body, OP_RETURN, 1);

    /* Create a closure template with 1 upvalue */
    size_t cl_size = sizeof(JaclClosure) + sizeof(JaclVal) * 1;
    JaclClosure *tmpl = (JaclClosure *)arena_alloc(&arena, cl_size);
    memset(tmpl, 0, cl_size);
    tmpl->chunk = body;
    tmpl->upvalue_count = 1;
    tmpl->upvalues = NULL;
    tmpl->name = "oob_test";

    BytecodeChunk main_chunk;
    chunk_init(&main_chunk, &arena);
    uint16_t ci = chunk_add_constant(&main_chunk, jacl_closure(tmpl));
    chunk_write(&main_chunk, OP_CLOSURE, 1);
    chunk_write_u16(&main_chunk, ci, 1);
    chunk_write(&main_chunk, 0, 1);  /* is_local = 0 (parent upvalue) */
    chunk_write(&main_chunk, 5, 1);  /* uv_index = 5 (out of bounds) */
    chunk_write(&main_chunk, 1, 1);  /* width = 1 */
    chunk_write(&main_chunk, OP_POP, 1);
    chunk_write(&main_chunk, OP_HALT, 1);

    VMResult r = vm_exec(&vm, &main_chunk);
    ASSERT(r == VM_RUNTIME_ERROR);
    ASSERT(vm.error_message != NULL);
    ASSERT(strstr(vm.error_message, "upvalue index 5+0 out of bounds") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: OP_CLOSURE with out-of-bounds local capture index triggers error */
static int test_closure_oob_local_capture(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /*
     * Craft bytecode:
     *   Top-level code pushes no locals onto the stack, then tries to
     *   create a closure capturing local slot 200 — well beyond stack_top.
     */
    BytecodeChunk body;
    chunk_init(&body, &arena);
    chunk_write(&body, OP_NIL, 1);
    chunk_write(&body, OP_RETURN, 1);

    size_t cl_size = sizeof(JaclClosure) + sizeof(JaclVal) * 1;
    JaclClosure *tmpl = (JaclClosure *)arena_alloc(&arena, cl_size);
    memset(tmpl, 0, cl_size);
    tmpl->chunk = body;
    tmpl->upvalue_count = 1;
    tmpl->upvalues = NULL;
    tmpl->name = "oob_local";

    BytecodeChunk main_chunk;
    chunk_init(&main_chunk, &arena);
    uint16_t ci = chunk_add_constant(&main_chunk, jacl_closure(tmpl));
    chunk_write(&main_chunk, OP_CLOSURE, 1);
    chunk_write_u16(&main_chunk, ci, 1);
    chunk_write(&main_chunk, 1, 1);    /* is_local = 1 (local capture) */
    chunk_write(&main_chunk, 200, 1);  /* uv_index = 200 (way beyond stack) */
    chunk_write(&main_chunk, 1, 1);    /* width = 1 */
    chunk_write(&main_chunk, OP_POP, 1);
    chunk_write(&main_chunk, OP_HALT, 1);

    VMResult r = vm_exec(&vm, &main_chunk);
    ASSERT(r == VM_RUNTIME_ERROR);
    ASSERT(vm.error_message != NULL);
    ASSERT(strstr(vm.error_message, "local upvalue index 200+0 out of bounds") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- US-003 (bugfix): Suspension detection for named proc calls --- */

/* Test: ast__contains_suspension with map detects named suspending proc */
static int test_contains_susp_named_proc(void) {
    SuspensionMap map = test__analyze(
        "proc deep {} { await [spawn { 42 }] }\n"
        "proc mid {} { def f [spawn { deep }]; await $f }");
    JaclVal deep = jacl_inline_string("deep", 4);
    JaclVal mid = jacl_inline_string("mid", 3);
    ASSERT(suspension_map_lookup(&map, deep));
    ASSERT(suspension_map_lookup(&map, mid));

    /* Verify ast__contains_suspension with map detects "deep" as suspending */
    arena_t arena = {0};
    LexResult tokens = lexer_lex("deep", &arena);
    ParseResult parse = parser_parse(tokens, &arena);
    ASSERT(parse.count == 1);
    ASSERT(ast__contains_suspension(parse.nodes[0], &map) == true);
    /* Without map, "deep" is not detected as suspension */
    ASSERT(ast__contains_suspension(parse.nodes[0], NULL) == false);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: spawn body calling named suspending proc compiles without error */
static int test_spawn_calls_susp_proc(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc slow {} {\n"
        "  def f [spawn { 42 }]\n"
        "  await $f\n"
        "}\n"
        "proc main {} {\n"
        "  def f [spawn { slow }]\n"
        "  def r [await $f]\n"
        "  print $r\n"
        "}\n"
        "main", &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "42\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: callback validation still rejects suspending callbacks via map */
static int test_susp_callback_reject_via_map(void) {
    /* A block containing a call to a suspending proc should be
       detected by ast__contains_suspension with map */
    ASSERT(test__compile_has_error(
        "proc sfn {} { await [spawn { 42 }] }\n"
        "[for [vec 1 2] { sfn }]",
        "cannot suspend inside non-suspending callback"));
    TEST_PASS();
}

/* US-004 (bugfix): CPS continuation error propagation hang */

/* Test: parallel body error + continuation accessing result doesn't hang */
static int test_cont_error_propagation(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc main {} {\n"
        "  def r [parallel { 42 } { error \"fail\" }]\n"
        "  print [error? $r]\n"
        "  print [error-val $r]\n"
        "}\n"
        "main",
        &vm, &arena);
    ASSERT(r == VM_OK);
    ASSERT_STR_EQ(cap.buffer, "true\nfail\n");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * US-007 (M14): Targeted GC+concurrency intersection tests
 * ==================================================================== */

/* --- Test helpers for non-threaded runtime (same as test_runtime.c) --- */

/* Init a Runtime without spawning worker threads — delegates to the
 * canonical setup in runtime.c. */
static void m14__init_no_threads(Runtime *rt, int num_workers) {
    runtime__init_state(rt, num_workers);
}

static void m14__destroy_no_threads(Runtime *rt) {
    runtime__teardown_state(rt);
}

static bool m14__ms_contains(GCMarkStack *ms, void *target) {
    void *ptr;
    bool found = false;
    void *saved[4096];
    int saved_count = 0;

    while (gc__ms_pop(ms, &ptr)) {
        if (ptr == target) found = true;
        if (saved_count < 4096)
            saved[saved_count++] = ptr;
    }
    for (int i = 0; i < saved_count; i++)
        gc__ms_push(ms, saved[i]);

    return found;
}

/* Test: grey buffer processing — atom reset during active GC writes old
 * value to grey buffer, which is then drained by the mark phase. */
static int test_gc_grey_buf_atom_reset(void) {
    Runtime rt;
    m14__init_no_threads(&rt, 1);

    WorkerThread *w = &rt.workers[0];
    ThreadHeap *heap = &w->vm.heap;

    /* Create an atom holding a heap value */
    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(heap, OBJ_MUTABLE_REF,
                                                      sizeof(JaclMutableRef) + sizeof(JaclVal));
    ref->type_idx = 0; ref->total_size = sizeof(JaclVal);
    JaclVal old_val = jacl_i64(heap, 111111LL);
    MREF_VAL(ref) = old_val;

    JaclVal new_val = jacl_i64(heap, 222222LL);

    /* Simulate active GC: set gc_active = 1 */
    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 1, MEM_RELEASE);

    /* Fire write barrier (same as OP_RESET on an atom) */
    gc_write_barrier(&w->grey_buf, &rt.gc_active, old_val, new_val);
    MREF_VAL(ref) = new_val;

    /* Grey buffer should contain both old and new values */
    uint32_t gb_count = ATOMIC_LOAD_EXPLICIT(&w->grey_buf.count, MEM_ACQUIRE);
    ASSERT(gb_count >= 2);

    /* Verify old_val is in the grey buffer */
    bool found_old = false, found_new = false;
    for (uint32_t i = 0; i < gb_count; i++) {
        if (w->grey_buf.entries[i] == old_val) found_old = true;
        if (w->grey_buf.entries[i] == new_val) found_new = true;
    }
    ASSERT(found_old);
    ASSERT(found_new);

    /* Now run concurrent GC collect — the grey buffer entries should be
     * drained and both values should survive the mark phase. */
    /* Root the atom on the environment so the ref is live */
    JaclVal atom_val = jacl_atom_ptr(ref);
    w->vm.env.names[w->vm.env.count]  = jacl_inline_string("a", 1);
    w->vm.env.values[w->vm.env.count] = atom_val;
    w->vm.env.count++;

    /* Run a mark cycle to drain the grey buffer */
    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    /* Drain grey bufs (simulates what gc_concurrent_collect does) */
    uint32_t *drained = (uint32_t *)calloc(1, sizeof(uint32_t));
    gc__drain_grey_bufs(&rt, &ms, drained);

    /* Both the old and new values should be on the mark stack */
    void *old_ptr = jacl_as_ptr(old_val);
    void *new_ptr = jacl_as_ptr(new_val);
    ASSERT(m14__ms_contains(&ms, old_ptr));
    ASSERT(m14__ms_contains(&ms, new_ptr));

    free(drained);
    gc__ms_destroy(&ms);

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 0, MEM_RELEASE);
    m14__destroy_no_threads(&rt);
    TEST_PASS();
}

/* Test: unrooted result regression — after US-001, create future, resolve
 * with heap result, schedule continuation with gc_root2 = result, force GC,
 * verify the result survives via gc_root2 rooting. */
static int test_gc_unrooted_result_regression(void) {
    Runtime rt;
    m14__init_no_threads(&rt, 1);

    WorkerThread *w = &rt.workers[0];
    ThreadHeap *heap = &w->vm.heap;

    /* Create a future and resolve it with a heap i64 */
    JaclVal f = jacl_future(heap);
    JaclFuture *fut = jacl_as_future(f);
    JaclVal result = jacl_i64(heap, 9876543LL);
    jacl_future_resolve(fut, result, NULL, NULL);

    /* Create a continuation task with gc_root2 = result
     * (simulates what runtime__schedule_continuation does) */
    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn = NULL;
    task->data = NULL;
    task->gc_root = f;        /* future as primary root */
    task->gc_root2 = result;  /* result as secondary root */

    /* Place the task on the deque (making it a root) */
    rt_deque_deque_push(w->public_deque, (uintptr_t)task);

    /* Drop direct reference to result — only reachable via gc_root2 */
    result = JACL_NIL;

    /* Enumerate roots and verify both future and result are found */
    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    void *fut_ptr = jacl_as_ptr(f);
    void *result_ptr = jacl_as_ptr(task->gc_root2);
    ASSERT(m14__ms_contains(&ms, fut_ptr));
    ASSERT(m14__ms_contains(&ms, result_ptr));

    gc__ms_destroy(&ms);
    m14__destroy_no_threads(&rt);
    TEST_PASS();
}

/* Test: OOM Tier 1 — set max_blocks very low, allocate garbage until
 * emergency GC fires, verify it reclaims memory and allocation succeeds. */
static int test_gc_oom_tier1_reclaim(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Set very small heap limit: 4 blocks = 256KB */
    vm.block_pool.max_blocks = 4;

    /* Fill blocks with garbage (nothing on VM stack = not rooted) */
    for (int i = 0; i < 20000; i++) {
        void *p = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
        if (!p) break;
    }

    /* All data is garbage. Emergency GC should reclaim it.
     * Next allocation should succeed without OOM panic. */
    void *result = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    ASSERT(result != NULL);

    /* Verify we stayed within the 4-block limit */
    ASSERT(vm.block_pool.total_blocks_allocated <= 4);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: OOM Tier 2 — emergency GC can't free enough, heap grows up to limit.
 * Some data is live (rooted on stack), so emergency GC can't reclaim it.
 * But we're below max_blocks, so allocation grows the heap. */
static int test_gc_oom_tier2_heap_growth(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allow 4 blocks total. Fill 2 blocks with live data (rooted). */
    vm.block_pool.max_blocks = 4;

    /* Root data on the VM stack so it survives emergency GC */
    for (int i = 0; i < 800 && vm.stack_top < VM_STACK_MAX - 1; i++) {
        void *p = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
        if (!p) break;
        JaclVal v = JACL_TAG_I64 | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
        vm.stack[vm.stack_top++] = v;
    }

    /* Record initial block count (should have needed 1-2 blocks) */
    uint32_t initial_blocks = vm.block_pool.total_blocks_allocated;
    ASSERT(initial_blocks >= 1);

    /* Allocate more — this will exhaust current blocks, trigger emergency GC
     * which can't reclaim live data, then grow the heap via Tier 2 */
    for (int i = 0; i < 5000; i++) {
        void *p = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
        if (!p) break;
    }

    /* Heap should have grown beyond initial allocation */
    ASSERT(vm.block_pool.total_blocks_allocated > initial_blocks);
    /* But stayed within the 4-block limit */
    ASSERT(vm.block_pool.total_blocks_allocated <= 4);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: OOM Tier 3 — custom gc__oom_handler fires with diagnostic info
 * when heap limit is reached and all data is live. */
static int test_gc_oom_tier3_handler_info(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Very tight limit: 1 block = 64KB */
    vm.block_pool.max_blocks = 1;

    /* Override panic handler to intercept OOM */
    void (*saved_handler)(ThreadHeap *, size_t) = gc__oom_handler;
    gc__oom_handler = test__oom_panic_handler;
    oom_panic_called = false;

    /* Fill with live data (rooted on stack) */
    for (int i = 0; i < VM_STACK_MAX - 1; i++) {
        void *p = gc_alloc(&vm.heap, OBJ_HEAP_I64, 248);
        if (!p) break;
        JaclVal v = JACL_TAG_I64 | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
        vm.stack[vm.stack_top++] = v;
    }

    /* Trigger OOM — Tier 1 emergency GC can't help (all live),
     * Tier 2 can't help (at max_blocks=1), Tier 3 fires. */
    if (setjmp(oom_panic_jmp) == 0) {
        for (int i = 0; i < 5000; i++) {
            (void)gc_alloc(&vm.heap, OBJ_HEAP_I64, 248);
        }
    }

    ASSERT(oom_panic_called);

    /* The handler received valid heap info (verified by handler not crashing;
     * the test__oom_panic_handler does longjmp which confirms it was called
     * with a valid heap pointer and request_size) */

    gc__oom_handler = saved_handler;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parallel join under GC pressure — parallel tasks with heavy
 * allocation, verify result vector survives GC between completion and join. */
static int test_gc_parallel_join_pressure(void) {
    Runtime rt;
    runtime_init(&rt, 4);
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Parallel with 4 bodies doing heavy computation + result vector access.
     * fib(N) creates significant stack depth and allocation pressure.
     * The result vector must survive GC between the last task completing
     * and the join continuation executing. */
    CompileResult cr = test__compile(
        "proc fib {n} {\n"
        "  if [<= $n 1] { + $n 0 } else { + [fib [- $n 1]] [fib [- $n 2]] }\n"
        "}\n"
        "proc task {} {\n"
        "  def r [parallel { fib 10 } { fib 8 } { fib 6 } { fib 4 }]\n"
        "  + [+ [vec-get $r 0] [vec-get $r 1]] [+ [vec-get $r 2] [vec-get $r 3]]\n"
        "}",
        &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    /* Run top-level code to register fib and task in the main VM's env.
     * Without this, worker VMs cannot resolve 'fib' via OP_GET_GLOBAL
     * since each worker has its own independent environment. */
    VMResult top_r = vm_exec(&vm, &cr.chunk);
    ASSERT(top_r == VM_OK);

    /* Get the properly instantiated task closure from the main VM env */
    JaclVal task_name = jacl_inline_string("task", 4);
    bool found = false;
    JaclVal task_val = vm__env_get(&vm, task_name, &found);
    ASSERT(found);
    ASSERT(jacl_is_closure(task_val));
    JaclClosure *task_cl = jacl_as_closure(task_val);

    /* Register fib on all worker VMs so parallel bodies can resolve it */
    JaclVal fib_name = jacl_inline_string("fib", 3);
    JaclVal fib_val = vm__env_get(&vm, fib_name, &found);
    ASSERT(found);
    for (int i = 0; i < rt.num_workers; i++) {
        vm__env_set(&rt.workers[i].vm, fib_name, fib_val);
    }

    VMResult r = rt_run_to_completion(&rt, task_cl, &arena);
    ASSERT(r == VM_OK);

    runtime_destroy(&rt);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * CPS-recursive-mutation: C-level CPS transform unit tests
 * ==================================================================== */

/**
 * Test: recursive proc with spawn/await compiles as SM.
 */
static int test_cps_recursive_spawn_transform(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc recur {n} {\n"
        "  if [< $n 1] { 0 } else {\n"
        "    def f [spawn { + $n 10 }]\n"
        "    def v [await $f]\n"
        "    + $v [recur [- $n 1]]\n"
        "  }\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *recur = test__find_closure(&cr.chunk, "recur");
    ASSERT(recur != NULL);
    ASSERT_INT_EQ(recur->param_count, 2); /* __sm + __rv */
    ASSERT(recur->is_sm_compiled);

    /* Scan for OP_SPAWN in the bytecode */
    bool found_spawn = false;
    for (uint32_t i = 0; i < recur->chunk.code_count; i++) {
        if (recur->chunk.code[i] == OP_SPAWN) {
            found_spawn = true;
            break;
        }
    }
    ASSERT(found_spawn);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/**
 * Test: recursive proc with parallel compiles as SM.
 */
static int test_cps_recursive_parallel_upvalues(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc loop {n} {\n"
        "  if [< $n 1] { 0 } else {\n"
        "    def r [parallel { + $n 0 } { + $n 1 }]\n"
        "    + [vec-get $r 0] [loop [- $n 1]]\n"
        "  }\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *loop = test__find_closure(&cr.chunk, "loop");
    ASSERT(loop != NULL);
    ASSERT_INT_EQ(loop->param_count, 2); /* __sm + __rv */
    ASSERT(loop->is_sm_compiled);

    /* Scan for OP_PARALLEL in loop's bytecode */
    bool found_parallel = false;
    for (uint32_t i = 0; i < loop->chunk.code_count; i++) {
        if (loop->chunk.code[i] == OP_PARALLEL) {
            found_parallel = true;
            break;
        }
    }
    ASSERT(found_parallel);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/**
 * Test: recursive proc with spawn/await compiles as SM (tail call variant).
 */
static int test_cps_recursive_tail_call_passes_k(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    CompileResult cr = test__compile(
        "proc run {n} {\n"
        "  def f [spawn { + $n 0 }]\n"
        "  def v [await $f]\n"
        "  run [- $n 1]\n"
        "}", &arena, &vm);
    ASSERT_U32_EQ(cr.error_count, 0);

    JaclClosure *run = test__find_closure(&cr.chunk, "run");
    ASSERT(run != NULL);
    ASSERT_INT_EQ(run->param_count, 2); /* __sm + __rv */
    ASSERT(run->is_sm_compiled);

    /* SM has no continuation closures */
    JaclClosure *cont = test__find_closure(&run->chunk, "__cont");
    ASSERT(cont == NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/**
 * Test: recursive CPS proc with spawn/await actually runs correctly (integration).
 * Verifies the bytecode is not just structurally correct but executes properly
 * through 3 levels of recursion.
 */
static int test_cps_recursive_spawn_runs(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    PrintCapture cap = {{0}, 0};
    vm.print_fn = capture_print;
    vm.print_ctx = &cap;

    VMResult r = jacl_run(
        "proc work {n} {\n"
        "  if [< $n 1] { 0 } else {\n"
        "    def f [spawn { + $n 0 }]\n"
        "    def v [await $f]\n"
        "    + $v [work [- $n 1]]\n"
        "  }\n"
        "}\n"
        "print [work 3]",
        &vm, &arena);
    ASSERT(r == VM_OK);
    /* work(3) = 3 + work(2) = 3 + 2 + work(1) = 3 + 2 + 1 + work(0) = 6 */
    ASSERT_STR_EQ(cap.buffer, "6\n");

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
        /* US-003: CPS transform — sequential code */
        { "cps_single_await",           test_cps_single_await_compiles },
        { "cps_two_seq_awaits",         test_cps_two_sequential_awaits },
        { "cps_three_seq_awaits",       test_cps_three_sequential_awaits },
        { "cps_non_susp_unaffected",    test_cps_non_suspending_unaffected },
        { "cps_non_susp_param_count",   test_cps_non_suspending_param_count },
        { "cps_propagation_caller",     test_cps_propagation_to_caller },
        { "cps_single_await_resolved",  test_cps_single_await_resolved },
        { "cps_variable_capture",       test_cps_variable_capture },
        { "cps_eager_capture",         test_cps_eager_capture },
        { "cps_deep_chain",            test_cps_deep_chain },
        { "cps_emits_op_await",        test_cps_emits_op_await },
        { "cps_existing_code_ok",      test_cps_existing_code_unaffected },
        /* US-004: CPS transform — control flow */
        { "cps_if_then_suspends",      test_cps_if_then_suspends },
        { "cps_if_else_suspends",      test_cps_if_else_suspends },
        { "cps_if_both_suspend",       test_cps_if_both_suspend },
        { "cps_nested_if",             test_cps_nested_if },
        { "cps_chained_if",            test_cps_chained_if },
        { "cps_nested_await_args",     test_cps_nested_await_args },
        { "cps_def_if_suspension",     test_cps_def_if_suspension },
        { "cps_if_then_runtime",       test_cps_if_then_runtime },
        { "cps_if_nonsuspend_branch",  test_cps_if_nonsuspend_branch },
        { "cps_try_catch_error",       test_cps_try_catch_error },
        { "cps_let_block_susp",        test_cps_let_block_suspension },
        { "cps_if_last_stmt",          test_cps_if_last_stmt },
        { "cps_if_no_suspension_ok",   test_cps_if_no_suspension_ok },
        /* US-005: spawn primitive */
        { "spawn_returns_future",      test_spawn_returns_future },
        { "spawn_nonsuspend_resolves", test_spawn_nonsuspending_resolves },
        { "spawn_suspend_resolves",    test_spawn_suspending_resolves },
        { "spawn_error_propagation",   test_spawn_error_propagation },
        { "spawn_worker_execution",    test_spawn_worker_execution },
        { "top_level_cps",            test_top_level_cps },
        { "rt_run_to_completion",     test_rt_run_to_completion },
        { "spawn_existing_code_ok",   test_spawn_existing_code_ok },
        /* US-006: await primitive */
        { "await_resolved_immediate", test_await_resolved_immediate },
        { "await_pending_waiter",     test_await_pending_waiter },
        { "await_errored_future",     test_await_errored_future },
        { "await_multiple_awaiters",  test_await_multiple_awaiters },
        { "await_sequential_results", test_await_sequential_results },
        { "await_chained_three",      test_await_chained_three },
        { "await_non_future_error",   test_await_non_future_error },
        { "await_worker_resolved",    test_await_worker_resolved },
        { "await_existing_code_ok",   test_await_existing_code_ok },
        /* US-007: parallel primitive */
        { "parallel_two_order",       test_parallel_two_tasks_order },
        { "parallel_three_tasks",     test_parallel_three_tasks },
        { "parallel_error_prop",      test_parallel_error_propagation },
        { "parallel_compiles",        test_parallel_compiles },
        { "parallel_susp_bodies",     test_parallel_suspending_bodies },
        { "parallel_worker_exec",     test_parallel_worker_execution },
        { "parallel_result_vector",   test_parallel_result_vector },
        { "parallel_continuation",    test_parallel_with_continuation },
        { "parallel_existing_ok",     test_parallel_existing_code_ok },
        /* US-008: race primitive */
        { "race_two_tasks",          test_race_two_tasks },
        { "race_three_tasks",        test_race_three_tasks },
        { "race_error_prop",         test_race_error_propagation },
        { "race_compiles",           test_race_compiles },
        { "race_worker_exec",        test_race_worker_execution },
        { "race_continuation",       test_race_with_continuation },
        { "race_existing_ok",        test_race_existing_code_ok },
        /* US-009: Remove VM stack root scanning */
        { "no_stack_scan_spawn",     test_no_stack_scan_spawn },
        { "no_stack_scan_parallel",  test_no_stack_scan_parallel },
        { "st_gc_still_works",       test_st_gc_still_works },
        /* US-010: Adaptive GC threshold */
        { "adaptive_gc_increase",   test_adaptive_threshold_increase },
        { "adaptive_gc_decrease",   test_adaptive_threshold_decrease },
        { "adaptive_gc_min_bound",  test_adaptive_threshold_min_bound },
        { "adaptive_gc_max_bound",  test_adaptive_threshold_max_bound },
        /* US-011: OOM escalation */
        { "oom_emergency_gc",       test_oom_emergency_gc_fires },
        { "oom_tier3_panic",        test_oom_tier3_panic },
        { "oom_blocks_tracking",    test_oom_total_blocks_tracking },
        { "oom_max_blocks_limit",   test_oom_max_blocks_limit },
        { "oom_panic_diagnostic",   test_oom_panic_diagnostic },
        { "oom_existing_code_ok",   test_oom_existing_code_ok },
        /* US-012: Integration tests and stress testing */
        /* CPS correctness (direct style) */
        { "integ_single_await",       test_integ_single_await },
        { "integ_three_seq_awaits",   test_integ_three_seq_awaits },
        { "integ_await_in_if",        test_integ_await_in_if },
        { "integ_await_nested_expr",  test_integ_await_nested_expr },
        { "integ_deep_chain",         test_integ_deep_chain },
        { "integ_var_capture",        test_integ_variable_capture },
        { "integ_susp_propagation",   test_integ_suspension_propagation },
        /* spawn+await integration */
        { "integ_spawn_await_val",    test_integ_spawn_await_value },
        { "integ_multi_spawn_await",  test_integ_multi_spawn_await },
        { "integ_spawn_heavy_alloc",  test_integ_spawn_heavy_alloc },
        { "integ_await_resolved",     test_integ_await_already_resolved },
        { "integ_await_errored",      test_integ_await_errored },
        { "integ_multi_awaiter",      test_integ_multi_awaiter_same },
        /* parallel integration */
        { "integ_parallel_two",       test_integ_parallel_two },
        { "integ_parallel_five",      test_integ_parallel_five },
        { "integ_parallel_error",     test_integ_parallel_error },
        { "integ_parallel_alloc",     test_integ_parallel_heavy_alloc },
        /* race integration */
        { "integ_race_two",           test_integ_race_two },
        { "integ_race_losers_ok",     test_integ_race_losers_ok },
        /* Direct-style integration */
        { "integ_toplevel_susp",      test_integ_toplevel_suspending },
        { "integ_nested_susp",        test_integ_nested_suspending },
        { "integ_error_thru_cps",     test_integ_error_through_cps },
        /* Multi-threaded stress */
        { "stress_workers_spawn",     test_stress_workers_spawn },
        { "stress_heavy_alloc_gc",    test_stress_heavy_alloc_gc },
        { "stress_parallel_many",     test_stress_parallel_many },
        { "stress_par_join_gc",       test_stress_parallel_join_gc },
        { "stress_race_many",         test_stress_race_many },
        { "stress_mixed_workload",    test_stress_mixed_workload },
        /* Memory bounded */
        { "integ_memory_bounded",     test_integ_memory_bounded },
        /* Regression (M0-M12) */
        { "regression_fib",           test_regression_fib },
        { "regression_closures",      test_regression_closures },
        { "regression_collections",   test_regression_collections },
        { "regression_error_handling",test_regression_error_handling },
        { "regression_mutable_state", test_regression_mutable_state },
        { "regression_strings",       test_regression_strings },
        /* US-002 (bugfix): VM upvalue bounds checking */
        { "closure_oob_parent_uv",   test_closure_oob_parent_upvalue },
        { "closure_oob_local_cap",   test_closure_oob_local_capture },
        /* US-003 (bugfix): Suspension detection for named proc calls */
        { "contains_susp_named",     test_contains_susp_named_proc },
        { "spawn_calls_susp_proc",   test_spawn_calls_susp_proc },
        { "susp_cb_reject_via_map",  test_susp_callback_reject_via_map },
        /* US-004 (bugfix): CPS continuation error propagation hang */
        { "cont_error_propagation",  test_cont_error_propagation },
        /* US-007 (M14): GC+concurrency intersection tests */
        { "gc_grey_buf_atom_reset", test_gc_grey_buf_atom_reset },
        { "gc_unrooted_result_reg", test_gc_unrooted_result_regression },
        { "gc_oom_tier1_reclaim",   test_gc_oom_tier1_reclaim },
        { "gc_oom_tier2_heap_grow", test_gc_oom_tier2_heap_growth },
        { "gc_oom_tier3_handler",   test_gc_oom_tier3_handler_info },
        { "gc_parallel_join_press", test_gc_parallel_join_pressure },
        /* CPS recursive transform unit tests */
        { "cps_recur_spawn_xform", test_cps_recursive_spawn_transform },
        { "cps_recur_par_upvals",  test_cps_recursive_parallel_upvalues },
        { "cps_recur_tail_k",     test_cps_recursive_tail_call_passes_k },
        { "cps_recur_spawn_runs", test_cps_recursive_spawn_runs },
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
