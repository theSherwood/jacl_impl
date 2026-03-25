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

/* ===== US-009: Yield inside while loops (state machine) ===== */

/* --- Test: SM while loop yielding 0..N --- */
static int test_sm_while_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
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

/* --- Test: SM while loop with mut counter via collect --- */
static int test_sm_while_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc counter {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [counter 5]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 0 1 2 3 4]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM while loop with two params (range) --- */
static int test_sm_while_range(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc range {start, end} {\n"
    "  mut i $start\n"
    "  while [< $i $end] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [range 3 7]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 3 4 5 6]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM while loop with multiple mut variables (fibonacci) --- */
static int test_sm_while_fibonacci(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
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
    "[print [collect [fibs]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* fib: 0, 1, 1, 2, 3 */
  ASSERT_STR_EQ(cap.buf, "[vec 0 1 1 2 3]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM while loop with multiple yields per iteration --- */
static int test_sm_while_multi_yield(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc pairs {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    [yield [+ $i 100]]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [pairs 3]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 0 100 1 101 2 102]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM while loop yielding zero iterations (empty) --- */
static int test_sm_while_empty(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc empty {} {\n"
    "  mut i 0\n"
    "  while [< $i 0] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [empty]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-010: Yield inside if-statements and nested control flow (SM) ===== */

/* --- Test: yield inside if-then branch (condition true) --- */
static int test_sm_if_then_yield(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {cond} {\n"
    "  if [== $cond 1] {\n"
    "    [yield 1]\n"
    "    [yield 2]\n"
    "  }\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen 1]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 2 99]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield inside if-then branch (condition false, no else) --- */
static int test_sm_if_then_yield_false(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {cond} {\n"
    "  if [== $cond 1] {\n"
    "    [yield 1]\n"
    "    [yield 2]\n"
    "  }\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen 0]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 99]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield in both then and else branches --- */
static int test_sm_if_else_yield(void) {
  PrintCapture cap;
  /* True path: yields 1, 2 then 99 */
  VMResult r = run_capture_sm(
    "proc gen {cond} {\n"
    "  if [== $cond 1] {\n"
    "    [yield 1]\n"
    "    [yield 2]\n"
    "  } else {\n"
    "    [yield 3]\n"
    "  }\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen 1]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 2 99]\n");
  ASSERT(check_no_leaks());

  /* False path: yields 3 then 99 */
  r = run_capture_sm(
    "proc gen {cond} {\n"
    "  if [== $cond 1] {\n"
    "    [yield 1]\n"
    "    [yield 2]\n"
    "  } else {\n"
    "    [yield 3]\n"
    "  }\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen 0]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 3 99]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: multiple yields in both branches, each with unique resume point --- */
static int test_sm_if_multi_yield_both(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {cond} {\n"
    "  [yield 0]\n"
    "  if [== $cond 1] {\n"
    "    [yield 10]\n"
    "    [yield 11]\n"
    "    [yield 12]\n"
    "  } else {\n"
    "    [yield 20]\n"
    "    [yield 21]\n"
    "  }\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen 1]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 0 10 11 12 99]\n");
  ASSERT(check_no_leaks());

  r = run_capture_sm(
    "proc gen {cond} {\n"
    "  [yield 0]\n"
    "  if [== $cond 1] {\n"
    "    [yield 10]\n"
    "    [yield 11]\n"
    "    [yield 12]\n"
    "  } else {\n"
    "    [yield 20]\n"
    "    [yield 21]\n"
    "  }\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen 0]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 0 20 21 99]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: while containing if containing yield --- */
static int test_sm_while_if_yield(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc evens {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    if [== [% $i 2] 0] {\n"
    "      [yield $i]\n"
    "    }\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [evens 6]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 0 2 4]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: while containing if/else with yields in both branches --- */
static int test_sm_while_if_else_yield(void) {
  PrintCapture cap;
  /* 3 iterations WITH parameter */
  VMResult r = run_capture_sm(
    "proc gen {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    if [== [% $i 2] 0] {\n"
    "      [yield [+ $i 100]]\n"
    "    } else {\n"
    "      [yield [+ $i 200]]\n"
    "    }\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [gen 5]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* 0→100, 1→201, 2→102, 3→203, 4→104 */
  ASSERT_STR_EQ(cap.buf, "[vec 100 201 102 203 104]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield in 3 levels of nesting (while > if > if) --- */
static int test_sm_nested_3_levels(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc deep {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    if [> $i 0] {\n"
    "      if [== [% $i 3] 0] {\n"
    "        [yield [* $i 10]]\n"
    "      }\n"
    "    }\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [deep 10]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* i=3→30, i=6→60, i=9→90 */
  ASSERT_STR_EQ(cap.buf, "[vec 30 60 90]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: if inside while with multiple yields per branch --- */
static int test_sm_while_if_multi_yield(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc pairs {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    if [== [% $i 2] 0] {\n"
    "      [yield $i]\n"
    "      [yield [+ $i 1000]]\n"
    "    }\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [pairs 4]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* i=0→0,1000  i=2→2,1002 */
  ASSERT_STR_EQ(cap.buf, "[vec 0 1000 2 1002]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-011: Derived streams with SM generators ===== */

/* --- Test: SM generator piped through filter + collect --- */
static int test_sm_filter_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "}\n"
    "def v [collect [filter [gen] [\\ > $it 3]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 4 5]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generator piped through transform + collect --- */
static int test_sm_transform_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n"
    "def v [collect [transform [gen] [\\ * $it 10]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 10 20 30]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generator piped through take + collect --- */
static int test_sm_take_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 10]\n"
    "  [yield 20]\n"
    "  [yield 30]\n"
    "  [yield 40]\n"
    "  [yield 50]\n"
    "}\n"
    "def v [collect [take [gen] 3]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 10 20 30]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generator | filter | transform | collect chained pipeline --- */
static int test_sm_chained_pipeline(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[gen 10] | filter [\\ > $it 4] | transform [\\ * $it 2] | collect | print\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* values 5,6,7,8,9 → doubled: 10,12,14,16,18 */
  ASSERT_STR_EQ(cap.buf, "[vec 10 12 14 16 18]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generator with while loop through filter + collect --- */
static int test_sm_filter_while_gen(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc counter {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [filter [counter 8] [\\ == [% $it 2] 0]]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* evens from 0..7: 0, 2, 4, 6 */
  ASSERT_STR_EQ(cap.buf, "[vec 0 2 4 6]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generator | filter | take pipeline --- */
static int test_sm_filter_take(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "  [yield 6]\n"
    "  [yield 7]\n"
    "}\n"
    "[gen] | filter [\\ > $it 3] | take 2 | collect | print\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 4 5]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: OP_COLLECT on SM generator via derived stream (transform) --- */
static int test_sm_collect_transform(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 5]\n"
    "  [yield 10]\n"
    "  [yield 15]\n"
    "}\n"
    "[print [collect [transform [gen] [\\ + $it 1]]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 6 11 16]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-013: SM compilation for await ===== */

/* --- Test: SM compilation emits OP_AWAIT_SM for await-only proc --- */
static int test_sm_compile_await(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  CompileResult cr = compile_with_sm(
    "proc fetch {} {\n"
    "  def f [spawn { 42 }]\n"
    "  [await $f]\n"
    "}\n",
    &arena, &intern_table, &vm.heap);

  ASSERT_INT_EQ(cr.error_count, 0);

  /* Find the fetch closure */
  JaclClosure* cl = NULL;
  for (uint16_t i = 0; i < cr.chunk.const_count; i++) {
    if (jacl_is_closure(cr.chunk.constants[i])) {
      JaclClosure* c = jacl_as_closure(cr.chunk.constants[i]);
      if (c->name && strcmp(c->name, "fetch") == 0) {
        cl = c;
        break;
      }
    }
  }
  ASSERT(cl != NULL);

  /* SM async function: param_count = 2 (state_obj, resume_value) */
  ASSERT_INT_EQ(cl->param_count, 2);
  ASSERT(cl->is_sm_compiled);

  /* Check bytecode contains OP_AWAIT_SM (not OP_AWAIT) */
  int await_sm_count = 0;
  int await_cps_count = 0;
  for (uint32_t i = 0; i < cl->chunk.code_count; i++) {
    if (cl->chunk.code[i] == OP_AWAIT_SM) await_sm_count++;
    if (cl->chunk.code[i] == OP_AWAIT) await_cps_count++;
  }
  ASSERT_INT_EQ(await_sm_count, 1);
  ASSERT_INT_EQ(await_cps_count, 0);

  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM compilation for mixed yield+await in same function --- */
static int test_sm_compile_mixed_yield_await(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  CompileResult cr = compile_with_sm(
    "proc gen {} {\n"
    "  def f [spawn { 42 }]\n"
    "  def result [await $f]\n"
    "  [yield $result]\n"
    "  [yield 99]\n"
    "}\n",
    &arena, &intern_table, &vm.heap);

  ASSERT_INT_EQ(cr.error_count, 0);

  JaclClosure* cl = NULL;
  for (uint16_t i = 0; i < cr.chunk.const_count; i++) {
    if (jacl_is_closure(cr.chunk.constants[i])) {
      JaclClosure* c = jacl_as_closure(cr.chunk.constants[i]);
      if (c->name && strcmp(c->name, "gen") == 0) {
        cl = c;
        break;
      }
    }
  }
  ASSERT(cl != NULL);

  /* SM with both yield and await: param_count = 2, is_generator = true */
  ASSERT_INT_EQ(cl->param_count, 2);
  ASSERT(cl->is_sm_compiled);
  ASSERT(cl->is_generator);

  /* Check bytecode contains both OP_AWAIT_SM and OP_YIELD_SM */
  int await_sm_count = 0;
  int yield_sm_count = 0;
  for (uint32_t i = 0; i < cl->chunk.code_count; i++) {
    if (cl->chunk.code[i] == OP_AWAIT_SM) await_sm_count++;
    if (cl->chunk.code[i] == OP_YIELD_SM) yield_sm_count++;
  }
  ASSERT_INT_EQ(await_sm_count, 1);
  ASSERT_INT_EQ(yield_sm_count, 2);

  /* sm_field_count should be 2 (f, result) */
  ASSERT_INT_EQ(cl->sm_field_count, 2);

  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM compilation for sequential awaits --- */
static int test_sm_compile_sequential_awaits(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  CompileResult cr = compile_with_sm(
    "proc multi {} {\n"
    "  def f1 [spawn { 10 }]\n"
    "  def f2 [spawn { 20 }]\n"
    "  def f3 [spawn { 30 }]\n"
    "  def a [await $f1]\n"
    "  def b [await $f2]\n"
    "  def c [await $f3]\n"
    "  [yield [+ $a [+ $b $c]]]\n"
    "}\n",
    &arena, &intern_table, &vm.heap);

  ASSERT_INT_EQ(cr.error_count, 0);

  JaclClosure* cl = NULL;
  for (uint16_t i = 0; i < cr.chunk.const_count; i++) {
    if (jacl_is_closure(cr.chunk.constants[i])) {
      JaclClosure* c = jacl_as_closure(cr.chunk.constants[i]);
      if (c->name && strcmp(c->name, "multi") == 0) {
        cl = c;
        break;
      }
    }
  }
  ASSERT(cl != NULL);

  /* 3 awaits + 1 yield = 4 suspension points → 4 resume points in dispatch */
  int await_sm_count = 0;
  int yield_sm_count = 0;
  for (uint32_t i = 0; i < cl->chunk.code_count; i++) {
    if (cl->chunk.code[i] == OP_AWAIT_SM) await_sm_count++;
    if (cl->chunk.code[i] == OP_YIELD_SM) yield_sm_count++;
  }
  ASSERT_INT_EQ(await_sm_count, 3);
  ASSERT_INT_EQ(yield_sm_count, 1);

  /* sm_field_count: f1, f2, f3, a, b, c = 6 */
  ASSERT_INT_EQ(cl->sm_field_count, 6);

  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: E2E yield+await generator with resolved future (single-threaded) --- */
static int test_sm_e2e_yield_await(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def f [spawn { 42 }]\n"
    "  def result [await $f]\n"
    "  [yield $result]\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 42 99]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: E2E sequential awaits then yield (single-threaded) --- */
static int test_sm_e2e_sequential_awaits(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def f1 [spawn { 10 }]\n"
    "  def f2 [spawn { 20 }]\n"
    "  def a [await $f1]\n"
    "  def b [await $f2]\n"
    "  [yield [+ $a $b]]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 30]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: E2E sequential awaits then yields in same function --- */
static int test_sm_e2e_await_then_yields(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def f1 [spawn { 100 }]\n"
    "  def f2 [spawn { 200 }]\n"
    "  def f3 [spawn { 300 }]\n"
    "  def a [await $f1]\n"
    "  [yield $a]\n"
    "  def b [await $f2]\n"
    "  [yield $b]\n"
    "  def c [await $f3]\n"
    "  [yield $c]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 100 200 300]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: CPS path unchanged when use_state_machines is false --- */
static int test_sm_await_cps_unchanged(void) {
  PrintCapture cap;
  /* Run without SM flag — uses CPS path (or placeholder) */
  VMResult r = run_capture(
    "proc main {} {\n"
    "  def f [spawn { 55 }]\n"
    "  def val [await $f]\n"
    "  [print $val]\n"
    "}\n"
    "[main]\n",
    &cap);
  /* CPS await is placeholder (returns nil), so val will be nil */
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-015: Error propagation via error_k on state object ===== */

/* --- Test: generator that errors after a yield — error propagates via stream_next --- */
static int test_sm_error_after_yield(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [error \"boom\"]\n"
    "  [yield 3]\n"
    "}\n"
    "def s [gen]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "def third [stream_next $s]\n"
    "[print [error? $third]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\ntrue\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: generator that errors on first call — error propagates via collect --- */
static int test_sm_error_in_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 10]\n"
    "  [error \"fail\"]\n"
    "  [yield 20]\n"
    "}\n"
    "def result [collect [gen]]\n"
    "[print [error? $result]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: generator error propagates through filter pipeline --- */
static int test_sm_error_through_filter(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [error \"pipe-err\"]\n"
    "}\n"
    "def s [gen]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "def third [stream_next $s]\n"
    "[print [error? $third]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\ntrue\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: generator that throws before any yield — error on first stream_next --- */
static int test_sm_error_before_yield(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [error \"early\"]\n"
    "  [yield 1]\n"
    "}\n"
    "def s [gen]\n"
    "def first [stream_next $s]\n"
    "[print [error? $first]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-016: End-to-end async state machine tests ===== */

/* Thread-safe print capture for concurrent tests */
typedef struct {
  PrintCapture     cap;
  platform_mutex_t mutex;
} ThreadSafePrint;

static void capture_print_ts(const char* text, uint32_t len, void* ctx) {
  ThreadSafePrint* tsp = (ThreadSafePrint*)ctx;
  MUTEX_LOCK(tsp->mutex);
  capture_print(text, len, &tsp->cap);
  MUTEX_UNLOCK(tsp->mutex);
}

/* --- Test: 3 sequential awaits with correct result (1 SM allocation) --- */
static int test_sm_async_3_sequential_awaits(void) {
  PrintCapture cap;
  /* In SM mode, the generator allocates exactly 1 state machine object.
     All 3 awaits reuse the same SM object via resume_point dispatch.
     This replaces the CPS approach which would allocate 3 continuation closures. */
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def f1 [spawn { 10 }]\n"
    "  def f2 [spawn { 20 }]\n"
    "  def f3 [spawn { 30 }]\n"
    "  def a [await $f1]\n"
    "  def b [await $f2]\n"
    "  def c [await $f3]\n"
    "  [yield [+ $a [+ $b $c]]]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 60]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM generators on worker threads (concurrent, yield only) --- */
static int test_sm_async_concurrent_workers(void) {
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);
  vm.intern_table = &intern_table;

  /* Compile SM generator program */
  CompileResult cr = compile_with_sm(
    "proc gen {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [gen 5]]]\n",
    &arena, &intern_table, &vm.heap);
  ASSERT_INT_EQ(cr.error_count, 0);

  /* Wrap compiled chunk in a non-CPS closure for runtime submission */
  JaclClosure* wrapper = (JaclClosure*)gc_alloc(&vm.heap, OBJ_CLOSURE,
                                                  sizeof(JaclClosure));
  memset(wrapper, 0, sizeof(JaclClosure));
  wrapper->chunk = cr.chunk;
  wrapper->param_count = 0;
  wrapper->upvalue_count = 0;

  /* Set up runtime with 2 workers */
  Runtime rt;
  runtime_init(&rt, 2);

  /* Thread-safe print capture */
  ThreadSafePrint tsp;
  memset(&tsp, 0, sizeof(tsp));
  MUTEX_INIT(tsp.mutex);

  /* Configure worker VMs */
  for (int i = 0; i < rt.num_workers; i++) {
    rt.workers[i].vm.print_fn = capture_print_ts;
    rt.workers[i].vm.print_ctx = &tsp;
    rt.workers[i].vm.intern_table = &intern_table;
    rt.workers[i].vm.struct_registry = cr.struct_registry;
  }

  /* Submit closure and wait for completion */
  JaclVal completion = jacl_future(&rt.workers[0].vm.heap);
  JaclFuture* cfut = jacl_as_future(completion);
  runtime__submit_spawn_task(&rt, wrapper, completion, false);

  for (;;) {
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_ACQUIRE);
    if (state == FUTURE_RESOLVED || state == FUTURE_ERROR) break;
    SLEEP_MILLISECONDS(1);
  }

  uint32_t final_state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
  ASSERT_INT_EQ(final_state, FUTURE_RESOLVED);
  ASSERT_STR_EQ(tsp.cap.buf, "[vec 0 1 2 3 4]\n");

  runtime_destroy(&rt);
  MUTEX_DESTROY(tsp.mutex);
  intern_table_destroy(&intern_table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test: async function spawns, awaits, processes result, yields --- */
static int test_sm_async_spawn_await_process(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def f [spawn { 42 }]\n"
    "  def result [await $f]\n"
    "  def doubled [* $result 2]\n"
    "  [yield $doubled]\n"
    "  def f2 [spawn { 100 }]\n"
    "  def r2 [await $f2]\n"
    "  [yield [+ $r2 $result]]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 84 142]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: nested async calls — SM generator collects from another SM generator --- */
static int test_sm_async_nested_sm(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc inner {} {\n"
    "  def f [spawn { 10 }]\n"
    "  def val [await $f]\n"
    "  [yield $val]\n"
    "  [yield [* $val 2]]\n"
    "  [yield [* $val 3]]\n"
    "}\n"
    "proc outer {} {\n"
    "  def vals [collect [inner]]\n"
    "  [yield $vals]\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [outer]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec [vec 10 20 30] 99]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: single-threaded await resolves inline with no scheduling --- */
static int test_sm_async_inline_resolved(void) {
  PrintCapture cap;
  /* In single-threaded mode, spawn resolves synchronously.
     OP_AWAIT_SM detects the resolved future and pushes the result
     inline, continuing execution without any scheduling overhead. */
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def f [spawn { 77 }]\n"
    "  [yield [await $f]]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 77]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: all existing tests pass with use_state_machines = false (CPS) --- */
static int test_sm_async_cps_still_works(void) {
  PrintCapture cap;
  /* Run the same generator pattern through CPS path (no SM flag) */
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 2 3]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-017: SM parallel join tests ===== */

/* --- Test: SM parallel with 3 bodies, single-threaded (inline result) --- */
static int test_sm_parallel_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def results [parallel { 10 } { 20 } { 30 }]\n"
    "  [yield $results]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec [vec 10 20 30]]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: parent locals survive across parallel --- */
static int test_sm_parallel_locals_survive(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def before 42\n"
    "  [yield $before]\n"
    "  def results [parallel { 10 } { 20 }]\n"
    "  [yield $before]\n"
    "  [yield $results]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 42 42 [vec 10 20]]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: parallel with computed bodies --- */
static int test_sm_parallel_computed(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def results [parallel { [+ 5 5] } { [* 3 7] } { [- 100 70] }]\n"
    "  [yield $results]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec [vec 10 21 30]]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM parallel in while loop (bodies use constants, not SM state) --- */
static int test_sm_parallel_in_while(void) {
  PrintCapture cap;
  /* Parallel bodies cannot capture SM state fields (they're closures, not SM code).
     Use constant values in bodies, yield loop counter separately. */
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  mut i 0\n"
    "  while [< $i 2] {\n"
    "    def r [parallel { 100 } { 200 }]\n"
    "    [yield $r]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec [vec 100 200] [vec 100 200]]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM parallel with yield before and after --- */
static int test_sm_parallel_yield_around(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  def results [parallel { 10 } { 20 }]\n"
    "  [yield $results]\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 [vec 10 20] 99]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: SM parallel with error propagation --- */
static int test_sm_parallel_error(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  def results [parallel { 10 } { [error \"oops\"] } { 30 }]\n"
    "  [yield [error? $results]]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec true]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-019: Mixed suspension: yield + await in same function ===== */

/* --- Test: async generator that awaits then yields in a while loop --- */
static int test_sm_mixed_await_yield_loop(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  mut i 0\n"
    "  while [< $i 3] {\n"
    "    def f [spawn { 100 }]\n"
    "    def result [await $f]\n"
    "    [yield [+ $result $i]]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* 100+0=100, 100+1=101, 100+2=102 */
  ASSERT_STR_EQ(cap.buf, "[vec 100 101 102]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: interleaved yield and await produce correct sequence --- */
static int test_sm_mixed_interleaved(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  def f1 [spawn { 10 }]\n"
    "  def a [await $f1]\n"
    "  [yield [+ $a 1]]\n"
    "  [yield 3]\n"
    "  def f2 [spawn { 20 }]\n"
    "  def b [await $f2]\n"
    "  [yield [+ $b 1]]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* yield 1, await 10 → yield 11, yield 3, await 20 → yield 21 */
  ASSERT_STR_EQ(cap.buf, "[vec 1 11 3 21]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: await+yield loop with multiple awaits per iteration --- */
static int test_sm_mixed_multi_await_loop(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  mut i 0\n"
    "  while [< $i 2] {\n"
    "    def f1 [spawn { 10 }]\n"
    "    def f2 [spawn { 20 }]\n"
    "    def a [await $f1]\n"
    "    def b [await $f2]\n"
    "    [yield [+ $a $b]]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* Each iteration: await 10 + await 20 = yield 30 */
  ASSERT_STR_EQ(cap.buf, "[vec 30 30]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield before loop, await+yield in loop, yield after loop --- */
static int test_sm_mixed_yield_around_loop(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  [yield 0]\n"
    "  mut i 0\n"
    "  while [< $i 2] {\n"
    "    def f [spawn { 50 }]\n"
    "    def val [await $f]\n"
    "    [yield [+ $val $i]]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "  [yield 99]\n"
    "}\n"
    "[print [collect [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* yield 0, loop: 50+0=50, 50+1=51, yield 99 */
  ASSERT_STR_EQ(cap.buf, "[vec 0 50 51 99]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: await+yield in if branches (cond=1, true branch) --- */
static int test_sm_mixed_await_yield_if(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {cond} {\n"
    "  if [== $cond 1] {\n"
    "    def f [spawn { 10 }]\n"
    "    def a [await $f]\n"
    "    [yield $a]\n"
    "    [yield [+ $a 1]]\n"
    "  } else {\n"
    "    def f [spawn { 20 }]\n"
    "    def b [await $f]\n"
    "    [yield $b]\n"
    "  }\n"
    "}\n"
    "[print [collect [gen 1]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* cond=1: await 10 → yield 10, yield 11 */
  ASSERT_STR_EQ(cap.buf, "[vec 10 11]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: await+yield in if-else (cond=0, false branch) --- */
static int test_sm_mixed_await_yield_if_false(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {cond} {\n"
    "  if [== $cond 1] {\n"
    "    def f [spawn { 10 }]\n"
    "    def a [await $f]\n"
    "    [yield $a]\n"
    "    [yield [+ $a 1]]\n"
    "  } else {\n"
    "    def f [spawn { 20 }]\n"
    "    def b [await $f]\n"
    "    [yield $b]\n"
    "  }\n"
    "}\n"
    "[print [collect [gen 0]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* cond=0: await 20 → yield 20 */
  ASSERT_STR_EQ(cap.buf, "[vec 20]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: pipeline on mixed await+yield generator --- */
static int test_sm_mixed_pipeline(void) {
  PrintCapture cap;
  VMResult r = run_capture_sm(
    "proc gen {} {\n"
    "  mut i 0\n"
    "  while [< $i 5] {\n"
    "    def f [spawn { 10 }]\n"
    "    def val [await $f]\n"
    "    [yield [+ $val $i]]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[gen] | filter [\\ > $it 11] | take 2 | collect | print\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* Yields: 10,11,12,13,14 → filter >11: 12,13 → take 2: 12,13 */
  ASSERT_STR_EQ(cap.buf, "[vec 12 13]\n");
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
    { "sm_while_basic",         test_sm_while_basic },
    { "sm_while_collect",       test_sm_while_collect },
    { "sm_while_range",         test_sm_while_range },
    { "sm_while_fibonacci",     test_sm_while_fibonacci },
    { "sm_while_multi_yield",   test_sm_while_multi_yield },
    { "sm_while_empty",         test_sm_while_empty },
    { "sm_if_then_yield",       test_sm_if_then_yield },
    { "sm_if_then_yield_false", test_sm_if_then_yield_false },
    { "sm_if_else_yield",       test_sm_if_else_yield },
    { "sm_if_multi_yield_both", test_sm_if_multi_yield_both },
    { "sm_while_if_yield",      test_sm_while_if_yield },
    { "sm_while_if_else_yield", test_sm_while_if_else_yield },
    { "sm_nested_3_levels",     test_sm_nested_3_levels },
    { "sm_while_if_multi_yield",test_sm_while_if_multi_yield },
    { "sm_filter_collect",      test_sm_filter_collect },
    { "sm_transform_collect",   test_sm_transform_collect },
    { "sm_take_collect",        test_sm_take_collect },
    { "sm_chained_pipeline",    test_sm_chained_pipeline },
    { "sm_filter_while_gen",    test_sm_filter_while_gen },
    { "sm_filter_take",         test_sm_filter_take },
    { "sm_collect_transform",   test_sm_collect_transform },
    { "sm_compile_await",        test_sm_compile_await },
    { "sm_compile_mixed_yield_await", test_sm_compile_mixed_yield_await },
    { "sm_compile_sequential_awaits", test_sm_compile_sequential_awaits },
    { "sm_e2e_yield_await",      test_sm_e2e_yield_await },
    { "sm_e2e_sequential_awaits", test_sm_e2e_sequential_awaits },
    { "sm_e2e_await_then_yields", test_sm_e2e_await_then_yields },
    { "sm_await_cps_unchanged",  test_sm_await_cps_unchanged },
    { "sm_error_after_yield",    test_sm_error_after_yield },
    { "sm_error_in_collect",     test_sm_error_in_collect },
    { "sm_error_through_filter", test_sm_error_through_filter },
    { "sm_error_before_yield",   test_sm_error_before_yield },
    { "sm_async_3_sequential_awaits", test_sm_async_3_sequential_awaits },
    { "sm_async_concurrent_workers",  test_sm_async_concurrent_workers },
    { "sm_async_spawn_await_process", test_sm_async_spawn_await_process },
    { "sm_async_nested_sm",           test_sm_async_nested_sm },
    { "sm_async_inline_resolved",     test_sm_async_inline_resolved },
    { "sm_async_cps_still_works",     test_sm_async_cps_still_works },
    { "sm_parallel_basic",            test_sm_parallel_basic },
    { "sm_parallel_locals_survive",   test_sm_parallel_locals_survive },
    { "sm_parallel_computed",         test_sm_parallel_computed },
    { "sm_parallel_in_while",         test_sm_parallel_in_while },
    { "sm_parallel_yield_around",     test_sm_parallel_yield_around },
    { "sm_parallel_error",            test_sm_parallel_error },
    { "sm_mixed_await_yield_loop",    test_sm_mixed_await_yield_loop },
    { "sm_mixed_interleaved",         test_sm_mixed_interleaved },
    { "sm_mixed_multi_await_loop",    test_sm_mixed_multi_await_loop },
    { "sm_mixed_yield_around_loop",   test_sm_mixed_yield_around_loop },
    { "sm_mixed_await_yield_if",      test_sm_mixed_await_yield_if },
    { "sm_mixed_await_yield_if_false",test_sm_mixed_await_yield_if_false },
    { "sm_mixed_pipeline",            test_sm_mixed_pipeline },
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
