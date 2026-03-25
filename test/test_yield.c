#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-009: OP_YIELD and stream creation ===== */

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

/* --- Test: basic generator yields 3 values then exhausts --- */
static int test_yield_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n"
    "def s [gen]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield in a loop --- */
static int test_yield_loop(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc counter {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s [counter 4]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n2\n3\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: stream exhaustion returns nil repeatedly --- */
static int test_yield_exhaustion(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 42]\n"
    "}\n"
    "def s [gen]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\nnil\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: calling generator returns a stream (not executing body) --- */
static int test_yield_lazy(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [print \"body\"]\n"
    "  [yield 1]\n"
    "}\n"
    "def s [gen]\n"
    "[print \"before\"]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* Body executes on first stream_next, so "body" prints before "1" but after "before" */
  ASSERT_STR_EQ(cap.buf, "before\nbody\n1\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield with arguments to generator proc --- */
static int test_yield_with_args(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc range {start, end} {\n"
    "  mut i $start\n"
    "  while [< $i $end] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s [range 3 6]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n4\n5\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield in nested conditionals --- */
static int test_yield_conditional(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc filt {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    if [== [% $i 2] 0] {\n"
    "      [yield $i]\n"
    "    }\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s [filt 6]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n2\n4\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: print stream value shows <stream> --- */
static int test_yield_print_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "}\n"
    "def s [gen]\n"
    "[print $s]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "<stream>\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: multiple independent generators --- */
static int test_yield_multiple_streams(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {x} {\n"
    "  [yield $x]\n"
    "  [yield [+ $x 10]]\n"
    "}\n"
    "def a [gen 1]\n"
    "def b [gen 2]\n"
    "[print [stream_next $a]]\n"
    "[print [stream_next $b]]\n"
    "[print [stream_next $a]]\n"
    "[print [stream_next $b]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n11\n12\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield string values --- */
static int test_yield_strings(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc words {} {\n"
    "  [yield \"hello\"]\n"
    "  [yield \"world\"]\n"
    "}\n"
    "def s [words]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\nworld\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: zero-arg generator that yields computed values --- */
static int test_yield_computed(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc fibs {} {\n"
    "  mut a 0\n"
    "  mut b 1\n"
    "  [yield $a]\n"
    "  [yield $b]\n"
    "  mut i 0\n"
    "  while [< $i 3] {\n"
    "    def c [+ $a $b]\n"
    "    [yield $c]\n"
    "    a :: $b\n"
    "    b :: $c\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s [fibs]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* fib: 0, 1, 1, 2, 3, then exhausted */
  ASSERT_STR_EQ(cap.buf, "0\n1\n1\n2\n3\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-007: State machine generator compilation ===== */

/* Helper: compile source with use_state_machines = true */
static CompileResult compile_with_sm(const char* src, arena_t* arena,
                                      JaclInternTable* intern_table,
                                      ThreadHeap* heap) {
  LexResult tokens = lexer_lex(src, arena);
  ParseResult parse = parser_parse(tokens, arena);

  CompileResult cr;
  chunk_init(&cr.chunk, arena);
  cr.error_count = parse.error_count;
  cr.suspending = false;

  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count);

  Compiler c;
  compiler__init(&c, &cr.chunk, arena, intern_table, heap);
  c.suspension_map = &suspension_map;
  c.use_state_machines = true;  /* Enable SM compilation */

  StructTypeRegistry* reg = (StructTypeRegistry*)arena_alloc(arena, sizeof(StructTypeRegistry));
  reg->count = 0;
  c.struct_registry = reg;

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&c, parse.nodes[i]);
    compiler__emit_byte(&c, OP_CHECK_ERROR, parse.nodes[i]->start.line);
    compiler__emit_u16(&c, 0, parse.nodes[i]->start.line);
  }
  compiler__emit_byte(&c, OP_HALT, 0);

  cr.error_count = c.error_count;
  cr.error_message = c.first_error;
  cr.struct_registry = c.struct_registry;
  return cr;
}

/* --- Test: SM compilation produces correct bytecode structure --- */
static int test_sm_compile_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  CompileResult cr = compile_with_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n",
    &arena, &intern_table, &vm.heap);

  ASSERT_INT_EQ(cr.error_count, 0);

  /* The chunk should have a closure constant. Find the generator closure. */
  JaclClosure* gen_cl = NULL;
  for (uint16_t i = 0; i < cr.chunk.const_count; i++) {
    if (jacl_is_closure(cr.chunk.constants[i])) {
      JaclClosure* cl = jacl_as_closure(cr.chunk.constants[i]);
      if (cl->name && strcmp(cl->name, "gen") == 0) {
        gen_cl = cl;
        break;
      }
    }
  }
  ASSERT(gen_cl != NULL);

  /* SM generator should have param_count = 2 (state_obj, resume_value) */
  ASSERT_INT_EQ(gen_cl->param_count, 2);

  /* sm_field_count should be 0 (no params, no locals in this generator) */
  ASSERT_INT_EQ(gen_cl->sm_field_count, 0);

  /* is_generator should be true */
  ASSERT(gen_cl->is_generator);

  /* Check the bytecode contains OP_YIELD_SM (not OP_YIELD) */
  int yield_sm_count = 0;
  int yield_cps_count = 0;
  for (uint32_t i = 0; i < gen_cl->chunk.code_count; i++) {
    if (gen_cl->chunk.code[i] == OP_YIELD_SM) yield_sm_count++;
    if (gen_cl->chunk.code[i] == OP_YIELD) yield_cps_count++;
  }
  ASSERT_INT_EQ(yield_sm_count, 3);
  ASSERT_INT_EQ(yield_cps_count, 0);

  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM compilation with params stores fields in state object --- */
static int test_sm_compile_with_params(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  CompileResult cr = compile_with_sm(
    "proc gen {x y} {\n"
    "  [yield $x]\n"
    "  [yield $y]\n"
    "}\n",
    &arena, &intern_table, &vm.heap);

  ASSERT_INT_EQ(cr.error_count, 0);

  JaclClosure* gen_cl = NULL;
  for (uint16_t i = 0; i < cr.chunk.const_count; i++) {
    if (jacl_is_closure(cr.chunk.constants[i])) {
      JaclClosure* cl = jacl_as_closure(cr.chunk.constants[i]);
      if (cl->name && strcmp(cl->name, "gen") == 0) {
        gen_cl = cl;
        break;
      }
    }
  }
  ASSERT(gen_cl != NULL);

  /* SM with 2 user params: sm_field_count should be 2 */
  ASSERT_INT_EQ(gen_cl->sm_field_count, 2);

  /* Bytecode should contain OP_GET_STATE_FIELD for param access */
  int get_sf_count = 0;
  for (uint32_t i = 0; i < gen_cl->chunk.code_count; i++) {
    if (gen_cl->chunk.code[i] == OP_GET_STATE_FIELD) get_sf_count++;
  }
  ASSERT(get_sf_count >= 2); /* at least once per param reference */

  /* OP_YIELD_SM count should be 2 */
  int yield_sm_count = 0;
  for (uint32_t i = 0; i < gen_cl->chunk.code_count; i++) {
    if (gen_cl->chunk.code[i] == OP_YIELD_SM) yield_sm_count++;
  }
  ASSERT_INT_EQ(yield_sm_count, 2);

  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generator can be manually driven via the VM --- */
static int test_sm_manual_drive(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  CompileResult cr = compile_with_sm(
    "proc gen {x} {\n"
    "  [yield $x]\n"
    "  [yield [+ $x 10]]\n"
    "}\n",
    &arena, &intern_table, &vm.heap);

  ASSERT_INT_EQ(cr.error_count, 0);

  /* Find the generator closure */
  JaclClosure* gen_cl = NULL;
  for (uint16_t i = 0; i < cr.chunk.const_count; i++) {
    if (jacl_is_closure(cr.chunk.constants[i])) {
      JaclClosure* cl = jacl_as_closure(cr.chunk.constants[i]);
      if (cl->name && strcmp(cl->name, "gen") == 0) {
        gen_cl = cl;
        break;
      }
    }
  }
  ASSERT(gen_cl != NULL);
  ASSERT_INT_EQ(gen_cl->sm_field_count, 1); /* one param: x */

  /* Create a state machine object with 1 field (for param x) */
  JaclVal sm_val = gc_alloc_state_machine(&vm.heap, 1);
  JaclStateMachine* sm = jacl_as_state_machine(sm_val);
  sm->fields[0] = jacl_i32(5); /* x = 5 */
  sm->sm_closure = jacl_closure(gen_cl);

  /* Call SM function: gen(state_obj, nil) — first call */
  vm__push(&vm, jacl_closure(gen_cl));  /* callee slot */
  vm__push(&vm, sm_val);                /* slot 0: state object */
  vm__push(&vm, JACL_NIL);             /* slot 1: resume value */

  CallFrame* frame = &vm.frames[vm.frame_count++];
  frame->closure    = gen_cl;
  frame->return_ip  = NULL;
  frame->stack_base = vm.stack_top - 2; /* 2 params */
  frame->chunk      = &gen_cl->chunk;
  vm.ip    = gen_cl->chunk.code;
  vm.chunk = &gen_cl->chunk;

  VMResult r = vm__run(&vm, 0);

  ASSERT_INT_EQ(r, VM_YIELD);
  /* First yield: should be x = 5 */
  ASSERT_INT_EQ(jacl_as_i32(vm.yield_value), 5);
  /* resume_point should be 1 */
  ASSERT_INT_EQ(sm->resume_point, 1);

  /* Reset stack and call again for second yield */
  vm.stack_top = 0;
  vm.frame_count = 0;

  vm__push(&vm, jacl_closure(gen_cl));
  vm__push(&vm, sm_val);
  vm__push(&vm, JACL_NIL);

  frame = &vm.frames[vm.frame_count++];
  frame->closure    = gen_cl;
  frame->return_ip  = NULL;
  frame->stack_base = vm.stack_top - 2;
  frame->chunk      = &gen_cl->chunk;
  vm.ip    = gen_cl->chunk.code;
  vm.chunk = &gen_cl->chunk;

  r = vm__run(&vm, 0);

  ASSERT_INT_EQ(r, VM_YIELD);
  /* Second yield: should be x + 10 = 15 */
  ASSERT_INT_EQ(jacl_as_i32(vm.yield_value), 15);
  ASSERT_INT_EQ(sm->resume_point, 2);

  /* Third call: generator exhausted, should return VM_OK */
  vm.stack_top = 0;
  vm.frame_count = 0;

  vm__push(&vm, jacl_closure(gen_cl));
  vm__push(&vm, sm_val);
  vm__push(&vm, JACL_NIL);

  frame = &vm.frames[vm.frame_count++];
  frame->closure    = gen_cl;
  frame->return_ip  = NULL;
  frame->stack_base = vm.stack_top - 2;
  frame->chunk      = &gen_cl->chunk;
  vm.ip    = gen_cl->chunk.code;
  vm.chunk = &gen_cl->chunk;

  r = vm__run(&vm, 0);
  ASSERT_INT_EQ(r, VM_OK);

  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-008: End-to-end SM generator with stream/collect ===== */

/* Helper: compile + run source with use_state_machines = true, capture output */
static VMResult run_capture_sm(const char* src, PrintCapture* cap) {
  cap->len = 0;
  cap->buf[0] = '\0';
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = cap;

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);
  vm.intern_table = &intern_table;

  CompileResult cr = compile_with_sm(src, &arena, &intern_table, &vm.heap);
  if (cr.error_count > 0) {
    intern_table_destroy(&intern_table);
    vm_destroy(&vm);
    arena_destroy(&arena);
    return VM_RUNTIME_ERROR;
  }

  vm.struct_registry = cr.struct_registry;
  VMResult result = vm_exec(&vm, &cr.chunk);
  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  return result;
}

/* --- Test: SM generator yielding 3 values, collected end-to-end --- */
static int test_sm_e2e_basic_collect(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  PrintCapture cap;
  cap.len = 0;
  cap.buf[0] = '\0';
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);
  vm.intern_table = &intern_table;

  const char* src =
    "proc gen {} {\n"
    "  [yield 10]\n"
    "  [yield 20]\n"
    "  [yield 30]\n"
    "}\n"
    "[print [collect [gen]]]\n";

  CompileResult cr = compile_with_sm(src, &arena, &intern_table, &vm.heap);
  ASSERT_INT_EQ(cr.error_count, 0);

  vm.struct_registry = cr.struct_registry;
  VMResult r = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 10 20 30]\n");

  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generator with params, collected end-to-end --- */
static int test_sm_e2e_with_params(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {x y} {\n"
    "  [yield $x]\n"
    "  [yield [+ $x $y]]\n"
    "  [yield $y]\n"
    "}\n"
    "[print [collect [gen 3 7]]]\n",
    &cap);

  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 3 10 7]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generator exhaustion returns nil for stream_next --- */
static int test_sm_e2e_exhaustion(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "}\n"
    "def s [gen]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);

  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Main --- */
typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "yield_basic",            test_yield_basic },
    { "yield_loop",             test_yield_loop },
    { "yield_exhaustion",       test_yield_exhaustion },
    { "yield_lazy",             test_yield_lazy },
    { "yield_with_args",        test_yield_with_args },
    { "yield_conditional",      test_yield_conditional },
    { "yield_print_stream",     test_yield_print_stream },
    { "yield_multiple_streams", test_yield_multiple_streams },
    { "yield_strings",          test_yield_strings },
    { "yield_computed",         test_yield_computed },
    { "sm_compile_basic",       test_sm_compile_basic },
    { "sm_compile_with_params", test_sm_compile_with_params },
    { "sm_manual_drive",        test_sm_manual_drive },
    { "sm_e2e_basic_collect",   test_sm_e2e_basic_collect },
    { "sm_e2e_with_params",     test_sm_e2e_with_params },
    { "sm_e2e_exhaustion",      test_sm_e2e_exhaustion },
  };

  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  int passed = 0;
  int failed = 0;

  for (int i = 0; i < total; i++) {
    printf("  %-35s ", tests[i].name);
    if (tests[i].fn()) {
      passed++;
    } else {
      failed++;
    }
  }

  printf("\n  %d/%d passed\n", passed, total);
  return failed > 0 ? 1 : 0;
}
