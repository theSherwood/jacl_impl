#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== Print capture helper ===== */

typedef struct {
  char     buf[4096];
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

/* Helper: run program, capture output, assert success */
static int run_ok(const char* source, PrintCapture* cap, const char* expected) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  if (cap) { cap->len = 0; cap->buf[0] = '\0'; }
  VM vm;
  vm_init(&vm, &arena);
  if (cap) {
    vm.print_fn  = capture_print;
    vm.print_ctx = cap;
  }

  VMResult result = jacl_run(source, &vm, &arena);
  if (result != VM_OK) {
    fprintf(stderr, "  Expected VM_OK but got error: %s\n",
            vm.error_message ? vm.error_message : "(null)");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  if (expected && cap) {
    if (strcmp(cap->buf, expected) != 0) {
      fprintf(stderr, "  Output mismatch:\n  Actual:   '%s'\n  Expected: '%s'\n",
              cap->buf, expected);
      vm_destroy(&vm);
      arena_destroy(&arena);
      return 0;
    }
  }

  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  return 1;
}

/* Helper: run program, expect compile or runtime error containing substring */
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

/* ===== US-001: Value representation — constructors, accessors, predicates ===== */

static int test_val_u32_roundtrip(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclVal v = jacl_u32(255);
  ASSERT(jacl_is_u32(v));
  ASSERT(!jacl_is_i32(v));
  ASSERT_U32_EQ(jacl_as_u32(v), 255);

  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  TEST_PASS();
}

static int test_val_u32_zero(void) {
  JaclVal v = jacl_u32(0);
  ASSERT(jacl_is_u32(v));
  ASSERT_U32_EQ(jacl_as_u32(v), 0);
  TEST_PASS();
}

static int test_val_u32_max(void) {
  JaclVal v = jacl_u32(UINT32_MAX);
  ASSERT(jacl_is_u32(v));
  ASSERT_U32_EQ(jacl_as_u32(v), UINT32_MAX);
  TEST_PASS();
}

static int test_val_i64_roundtrip(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclVal v = jacl_i64(&heap, 123456789LL);
  ASSERT(jacl_is_i64(v));
  ASSERT(!jacl_is_i32(v));
  ASSERT_I64_EQ(jacl_as_i64(v), 123456789LL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  TEST_PASS();
}

static int test_val_i64_negative(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclVal v = jacl_i64(&heap, -987654321LL);
  ASSERT(jacl_is_i64(v));
  ASSERT_I64_EQ(jacl_as_i64(v), -987654321LL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  TEST_PASS();
}

static int test_val_u64_roundtrip(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclVal v = jacl_u64(&heap, 999999999999ULL);
  ASSERT(jacl_is_u64(v));
  ASSERT(!jacl_is_i64(v));
  ASSERT_U64_EQ(jacl_as_u64(v), 999999999999ULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  TEST_PASS();
}

static int test_val_f64_roundtrip(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclVal v = jacl_f64(&heap, 3.14159);
  ASSERT(jacl_is_f64(v));
  ASSERT(!jacl_is_f32(v));
  ASSERT(jacl_as_f64(v) == 3.14159);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  TEST_PASS();
}

static int test_val_predicates_exclusive(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclVal u32v = jacl_u32(1);
  JaclVal i64v = jacl_i64(&heap, 1);
  JaclVal u64v = jacl_u64(&heap, 1);
  JaclVal f64v = jacl_f64(&heap, 1.0);

  /* u32 should only be u32 */
  ASSERT(jacl_is_u32(u32v));
  ASSERT(!jacl_is_i64(u32v));
  ASSERT(!jacl_is_u64(u32v));
  ASSERT(!jacl_is_f64(u32v));

  /* i64 should only be i64 */
  ASSERT(!jacl_is_u32(i64v));
  ASSERT(jacl_is_i64(i64v));
  ASSERT(!jacl_is_u64(i64v));
  ASSERT(!jacl_is_f64(i64v));

  /* u64 should only be u64 */
  ASSERT(!jacl_is_u32(u64v));
  ASSERT(!jacl_is_i64(u64v));
  ASSERT(jacl_is_u64(u64v));
  ASSERT(!jacl_is_f64(u64v));

  /* f64 should only be f64 */
  ASSERT(!jacl_is_u32(f64v));
  ASSERT(!jacl_is_i64(f64v));
  ASSERT(!jacl_is_u64(f64v));
  ASSERT(jacl_is_f64(f64v));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  TEST_PASS();
}

/* ===== US-006: Typed def for all numeric types ===== */

static int test_def_i64_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 x 42\n"
    "[print [to-string $x]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_def_u32_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def u32 x 255\n"
    "[print [to-string $x]]",
    &cap, "255\n"));
  TEST_PASS();
}

static int test_def_u64_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def u64 x 1000000\n"
    "[print [to-string $x]]",
    &cap, "1000000\n"));
  TEST_PASS();
}

static int test_def_f64_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def f64 x 3.14\n"
    "[print [to-string $x]]",
    &cap, "3.14\n"));
  TEST_PASS();
}

static int test_def_i32_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i32 x 99\n"
    "[print [to-string $x]]",
    &cap, "99\n"));
  TEST_PASS();
}

static int test_def_f32_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def f32 x 2.5\n"
    "[print [to-string $x]]",
    &cap, "2.5\n"));
  TEST_PASS();
}

static int test_def_untyped_unchanged(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def x 42\n"
    "[print $x]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_def_typed_error_str_to_i64(void) {
  ASSERT(run_err(
    "def i64 x \"hello\"",
    "type error: expected i64, got str"));
  TEST_PASS();
}

static int test_def_typed_error_str_to_f64(void) {
  ASSERT(run_err(
    "def f64 x \"hello\"",
    "type error: expected f64, got str"));
  TEST_PASS();
}

/* ===== US-006: Typed def in local scope ===== */

static int test_def_i64_local(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  def i64 x 100\n"
    "  print [to-string $x]\n"
    "}\n"
    "[t]",
    &cap, "100\n"));
  TEST_PASS();
}

static int test_def_f64_local(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  def f64 x 2.5\n"
    "  print [to-string $x]\n"
    "}\n"
    "[t]",
    &cap, "2.5\n"));
  TEST_PASS();
}

/* ===== US-007: Typed mut and type-checked set! ===== */

static int test_mut_i64_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "mut i64 x 0\n"
    "x :: 42\n"
    "[print [to-string $x]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_mut_f64_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "mut f64 x 0.0\n"
    "x :: 3.14\n"
    "[print [to-string $x]]",
    &cap, "3.14\n"));
  TEST_PASS();
}

static int test_mut_u64_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "mut u64 x 0\n"
    "x :: 100\n"
    "[print [to-string $x]]",
    &cap, "100\n"));
  TEST_PASS();
}

static int test_mut_u32_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "mut u32 x 0\n"
    "x :: 77\n"
    "[print [to-string $x]]",
    &cap, "77\n"));
  TEST_PASS();
}

static int test_set_type_error_str_to_i64(void) {
  ASSERT(run_err(
    "mut i64 x 0\n"
    "x :: \"hello\"",
    "type error: cannot assign str to i64 binding"));
  TEST_PASS();
}

static int test_set_type_error_dyn_to_i64(void) {
  ASSERT(run_err(
    "mut i64 x 0\n"
    "def y 42\n"
    "x :: $y",
    "type error: cannot assign dyn to i64 binding"));
  TEST_PASS();
}

static int test_set_typed_to_typed_ok(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut i64 x 0\n"
    "  def i64 y 99\n"
    "  x :: $y\n"
    "  print [to-string $x]\n"
    "}\n"
    "[t]",
    &cap, "99\n"));
  TEST_PASS();
}

static int test_set_untyped_mut_no_checking(void) {
  PrintCapture cap;
  /* Untyped mut should accept any value type */
  ASSERT(run_ok(
    "mut x 0\n"
    "x :: \"hello\"\n"
    "[print $x]",
    &cap, "hello\n"));
  TEST_PASS();
}

/* ===== US-007: Typed mut local in proc ===== */

static int test_mut_i64_local(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut i64 n 0\n"
    "  n :: 10\n"
    "  set n [+ $n 5]\n"
    "  print [to-string $n]\n"
    "}\n"
    "[t]",
    &cap, "15\n"));
  TEST_PASS();
}

/* ===== US-008: Typed arithmetic ===== */

static int test_arith_i64_add(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 a 10\n"
    "def i64 b 20\n"
    "[print [to-string [+ $a $b]]]",
    &cap, "30\n"));
  TEST_PASS();
}

static int test_arith_i64_sub(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 a 50\n"
    "def i64 b 30\n"
    "[print [to-string [- $a $b]]]",
    &cap, "20\n"));
  TEST_PASS();
}

static int test_arith_i64_mul(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 a 7\n"
    "def i64 b 6\n"
    "[print [to-string [* $a $b]]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_arith_i64_div(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 a 100\n"
    "def i64 b 4\n"
    "[print [to-string [/ $a $b]]]",
    &cap, "25\n"));
  TEST_PASS();
}

static int test_arith_i64_mod(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 a 17\n"
    "def i64 b 5\n"
    "[print [to-string [% $a $b]]]",
    &cap, "2\n"));
  TEST_PASS();
}

static int test_arith_f64_add(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def f64 a 1.5\n"
    "def f64 b 2.5\n"
    "[print [to-string [+ $a $b]]]",
    &cap, "4\n"));
  TEST_PASS();
}

static int test_arith_f64_mul(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def f64 a 3.0\n"
    "def f64 b 4.0\n"
    "[print [to-string [* $a $b]]]",
    &cap, "12\n"));
  TEST_PASS();
}

static int test_arith_f64_div(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def f64 a 10.0\n"
    "def f64 b 4.0\n"
    "[print [to-string [/ $a $b]]]",
    &cap, "2.5\n"));
  TEST_PASS();
}

static int test_arith_u64_div(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def u64 a 100\n"
    "def u64 b 3\n"
    "[print [to-string [/ $a $b]]]",
    &cap, "33\n"));
  TEST_PASS();
}

static int test_arith_u64_lt(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def u64 a 10\n"
    "def u64 b 20\n"
    "[print [< $a $b]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_arith_i64_comparison(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 a 10\n"
    "def i64 b 20\n"
    "[print [< $a $b]]\n"
    "[print [> $a $b]]\n"
    "[print [== $a $a]]",
    &cap, "true\nfalse\ntrue\n"));
  TEST_PASS();
}

static int test_arith_type_mismatch_i64_f64(void) {
  ASSERT(run_err(
    "def i64 a 10\n"
    "def f64 b 20.0\n"
    "[+ $a $b]",
    "type error: cannot add i64 and f64"));
  TEST_PASS();
}

static int test_arith_type_mismatch_i64_dyn(void) {
  ASSERT(run_err(
    "def i64 a 10\n"
    "def b 20\n"
    "[+ $a $b]",
    "type error: cannot add i64 and dyn"));
  TEST_PASS();
}

static int test_arith_contextual_literal(void) {
  PrintCapture cap;
  /* [+ $a 1] where a is i64 — literal 1 adopts i64 via contextual typing */
  ASSERT(run_ok(
    "def i64 a 10\n"
    "[print [to-string [+ $a 1]]]",
    &cap, "11\n"));
  TEST_PASS();
}

static int test_arith_nested_i64(void) {
  PrintCapture cap;
  /* [+ [* $a $b] $c] all i64 */
  ASSERT(run_ok(
    "def i64 a 3\n"
    "def i64 b 4\n"
    "def i64 c 5\n"
    "[print [to-string [+ [* $a $b] $c]]]",
    &cap, "17\n"));
  TEST_PASS();
}

static int test_arith_nested_f64(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def f64 a 2.0\n"
    "def f64 b 3.0\n"
    "def f64 c 1.0\n"
    "[print [to-string [- [* $a $b] $c]]]",
    &cap, "5\n"));
  TEST_PASS();
}

static int test_arith_i64_neg(void) {
  PrintCapture cap;
  /* Literal 0 must be typed as i64 via a typed def to match $a */
  ASSERT(run_ok(
    "def i64 a 42\n"
    "def i64 z 0\n"
    "[print [to-string [- $z $a]]]",
    &cap, "-42\n"));
  TEST_PASS();
}

static int test_arith_i64_div_by_zero(void) {
  ASSERT(run_err(
    "def i64 a 42\n"
    "def i64 b 0\n"
    "[/ $a $b]",
    "division by zero"));
  TEST_PASS();
}

static int test_arith_f64_div_by_zero(void) {
  ASSERT(run_err(
    "def f64 a 42.0\n"
    "def f64 b 0.0\n"
    "[/ $a $b]",
    "division by zero"));
  TEST_PASS();
}

/* ===== US-008: Result type propagation and inference ===== */

static int test_arith_result_type_inference(void) {
  PrintCapture cap;
  /* y = [+ $a $b] where a,b are i64 -> y inferred as i64 */
  ASSERT(run_ok(
    "def i64 a 10\n"
    "def i64 b 20\n"
    "def y [+ $a $b]\n"
    "[print [to-string $y]]",
    &cap, "30\n"));
  TEST_PASS();
}

/* ===== US-009: to builtin for type conversions ===== */

static int test_to_i64_from_i32(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i32 n 42\n"
    "def i64 big [to i64 $n]\n"
    "[print [to-string $big]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_to_i64_from_u32(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def u32 n 255\n"
    "def i64 big [to i64 $n]\n"
    "[print [to-string $big]]",
    &cap, "255\n"));
  TEST_PASS();
}

static int test_to_f64_from_i64(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 n 42\n"
    "def f64 x [to f64 $n]\n"
    "[print [to-string $x]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_to_i64_from_f64(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def f64 x 3.9\n"
    "def i64 n [to i64 $x]\n"
    "[print [to-string $n]]",
    &cap, "3\n"));
  TEST_PASS();
}

static int test_to_dyn_from_i64(void) {
  PrintCapture cap;
  /* Box an i64 into dyn */
  ASSERT(run_ok(
    "def i64 n 42\n"
    "def d [to dyn $n]\n"
    "[print $d]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_to_i64_from_dyn(void) {
  PrintCapture cap;
  /* Unbox a dyn (i32 at runtime) into i64 */
  ASSERT(run_ok(
    "def x 42\n"
    "def i64 n [to i64 $x]\n"
    "[print [to-string $n]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_to_dyn_dyn_noop(void) {
  PrintCapture cap;
  /* to dyn of dyn is a no-op */
  ASSERT(run_ok(
    "def x 42\n"
    "def y [to dyn $x]\n"
    "[print $y]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_to_u32_from_i32(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i32 n 42\n"
    "def u32 x [to u32 $n]\n"
    "[print [to-string $x]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_to_f32_from_i32(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i32 n 42\n"
    "def f32 x [to f32 $n]\n"
    "[print [to-string $x]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_to_error_str_to_i64(void) {
  ASSERT(run_err(
    "def str s \"hello\"\n"
    "def i64 x [to i64 $s]",
    "type error: cannot convert str to i64"));
  TEST_PASS();
}

static int test_to_arity_error(void) {
  ASSERT(run_err(
    "[to i64]",
    "to"));
  TEST_PASS();
}

/* ===== US-010: Typed proc — return type and param types ===== */

static int test_proc_i64_return(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc i64 add {i64 a, i64 b} { + $a $b }\n"
    "[print [to-string [add 3 4]]]",
    &cap, "7\n"));
  TEST_PASS();
}

static int test_proc_f64_return(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc f64 avg {f64 a, f64 b} { / [+ $a $b] 2.0 }\n"
    "[print [to-string [avg 10.0 20.0]]]",
    &cap, "15\n"));
  TEST_PASS();
}

static int test_proc_callsite_contextual_typing(void) {
  PrintCapture cap;
  /* Literals at call site adopt param types */
  ASSERT(run_ok(
    "proc i64 dbl {i64 n} { + $n $n }\n"
    "[print [to-string [dbl 21]]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_proc_callsite_type_error(void) {
  ASSERT(run_err(
    "proc i64 add {i64 a, i64 b} { + $a $b }\n"
    "[add \"x\" 2]",
    "type error: argument 1 of add expected i64, got str"));
  TEST_PASS();
}

static int test_proc_callsite_dyn_error(void) {
  ASSERT(run_err(
    "proc i64 add {i64 a, i64 b} { + $a $b }\n"
    "def x 42\n"
    "[add $x 2]",
    "type error:"));
  TEST_PASS();
}

static int test_proc_return_type_error(void) {
  ASSERT(run_err(
    "proc i64 bad {} { \"hello\" }",
    "type error: proc bad declared return type i64, but body returns str"));
  TEST_PASS();
}

static int test_proc_return_type_at_callsite(void) {
  PrintCapture cap;
  /* Return type propagates: def y [add 1 2] where add returns i64 -> y is i64 */
  ASSERT(run_ok(
    "proc i64 add {i64 a, i64 b} { + $a $b }\n"
    "def y [add 1 2]\n"
    "[print [to-string $y]]",
    &cap, "3\n"));
  TEST_PASS();
}

static int test_proc_mixed_params(void) {
  PrintCapture cap;
  /* Mixed typed/untyped params */
  ASSERT(run_ok(
    "proc f {i64 n, label} {\n"
    "  print $label\n"
    "  print [to-string $n]\n"
    "}\n"
    "[f 42 \"val:\"]",
    &cap, "val:\n42\n"));
  TEST_PASS();
}

static int test_proc_untyped_unchanged(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc add {a, b} { + $a $b }\n"
    "[print [add 3 4]]",
    &cap, "7\n"));
  TEST_PASS();
}

/* ===== US-006/US-008: Contextual typing — literals adopt expected types ===== */

static int test_contextual_i64_literal_in_def(void) {
  PrintCapture cap;
  /* Integer literal in i64 def compiles as i64 constant */
  ASSERT(run_ok(
    "def i64 x 100\n"
    "[print [to-string [+ $x 1]]]",
    &cap, "101\n"));
  TEST_PASS();
}

static int test_contextual_f64_literal_in_def(void) {
  PrintCapture cap;
  /* Float literal in f64 def compiles as f64 constant */
  ASSERT(run_ok(
    "def f64 x 2.5\n"
    "[print [to-string [+ $x 0.5]]]",
    &cap, "3\n"));
  TEST_PASS();
}

static int test_contextual_literal_in_set(void) {
  PrintCapture cap;
  /* set! passes expected type to RHS literal */
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut i64 x 0\n"
    "  x :: 99\n"
    "  print [to-string $x]\n"
    "}\n"
    "[t]",
    &cap, "99\n"));
  TEST_PASS();
}

/* ===== US-006: Type inference for untyped def ===== */

static int test_type_inference_from_typed_var(void) {
  PrintCapture cap;
  /* Untyped def infers type from typed RHS */
  ASSERT(run_ok(
    "def i64 a 42\n"
    "def b [+ $a 0]\n"
    "[print [to-string [+ $b 1]]]",
    &cap, "43\n"));
  TEST_PASS();
}

static int test_type_inference_from_typed_proc(void) {
  PrintCapture cap;
  /* Inferred type from proc return type */
  ASSERT(run_ok(
    "proc i64 f {} { 42 }\n"
    "def r [f]\n"
    "[print [to-string [+ $r 8]]]",
    &cap, "50\n"));
  TEST_PASS();
}

/* ===== US-005/US-010: Typed closures — typed params and upvalue capture ===== */

static int test_typed_closure_capture(void) {
  PrintCapture cap;
  /* Typed mut captured through closure retains type;
     get must be typed i64 so its unboxed return is handled correctly */
  ASSERT(run_ok(
    "proc test {} {\n"
    "  mut i64 n 0\n"
    "  proc inc {} { set n [+ $n 1] }\n"
    "  proc i64 get {} { + $n 0 }\n"
    "  inc\n"
    "  inc\n"
    "  print [to-string [get]]\n"
    "}\n"
    "[test]",
    &cap, "2\n"));
  TEST_PASS();
}

static int test_typed_closure_param(void) {
  PrintCapture cap;
  /* Closure with typed params */
  ASSERT(run_ok(
    "proc test {} {\n"
    "  proc i64 add {i64 a, i64 b} { + $a $b }\n"
    "  print [to-string [add 10 20]]\n"
    "}\n"
    "[test]",
    &cap, "30\n"));
  TEST_PASS();
}

static int test_typed_upvalue_transitive(void) {
  PrintCapture cap;
  /* Inner closure captures typed mut from grandparent */
  ASSERT(run_ok(
    "proc outer {} {\n"
    "  mut i64 x 0\n"
    "  proc mid {} {\n"
    "    proc inn {} { set x [+ $x 10] }\n"
    "    inn\n"
    "  }\n"
    "  mid\n"
    "  print [to-string [+ $x 0]]\n"
    "}\n"
    "[outer]",
    &cap, "10\n"));
  TEST_PASS();
}

/* ===== US-011: Print/to-string for all new types ===== */

static int test_print_i64(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 x 42\n"
    "[print [to-string $x]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_print_u32(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def u32 x 255\n"
    "[print [to-string $x]]",
    &cap, "255\n"));
  TEST_PASS();
}

static int test_print_u64(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def u64 x 9999\n"
    "[print [to-string $x]]",
    &cap, "9999\n"));
  TEST_PASS();
}

static int test_print_f64(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def f64 x 3.14\n"
    "[print [to-string $x]]",
    &cap, "3.14\n"));
  TEST_PASS();
}

static int test_print_string_interpolation_typed(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def i64 x 42\n"
    "[print \"x is $x\"]",
    &cap, "x is 42\n"));
  TEST_PASS();
}

static int test_print_no_regression_i32(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [to-string 99]]",
    &cap, "99\n"));
  TEST_PASS();
}

static int test_print_no_regression_f32(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [to-string 2.5]]",
    &cap, "2.5\n"));
  TEST_PASS();
}

/* ===== US-012: Interaction with existing features ===== */

static int test_typed_in_try(void) {
  PrintCapture cap;
  /* Typed values in error handling (try) — use to-string to box
     before try boundary since try operates on tagged values */
  ASSERT(run_ok(
    "def i64 x 42\n"
    "def r [try { to-string [+ $x 1] } err { \"error\" }]\n"
    "[print $r]",
    &cap, "43\n"));
  TEST_PASS();
}

static int test_typed_to_dyn_for_collection(void) {
  PrintCapture cap;
  /* Typed value converted to dyn for collection storage */
  ASSERT(run_ok(
    "def i64 x 42\n"
    "def v [vec [to dyn $x]]\n"
    "[print $v]",
    &cap, "[vec 42]\n"));
  TEST_PASS();
}

static int test_typed_if_branch(void) {
  PrintCapture cap;
  /* Typed comparison drives if/else */
  ASSERT(run_ok(
    "def i64 a 10\n"
    "def i64 b 20\n"
    "def r [if [< $a $b] { \"less\" } { \"greater\" }]\n"
    "[print $r]",
    &cap, "less\n"));
  TEST_PASS();
}

static int test_typed_while_loop(void) {
  PrintCapture cap;
  /* Typed mut in while loop */
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut i64 i 0\n"
    "  mut i64 s 0\n"
    "  while [< $i 5] {\n"
    "    set s [+ $s $i]\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "  print [to-string [+ $s 0]]\n"
    "}\n"
    "[t]",
    &cap, "10\n"));
  TEST_PASS();
}

/* ===== US-012: Edge cases ===== */

static int test_i64_large_value(void) {
  PrintCapture cap;
  /* Large i64 value that doesn't fit in i32 */
  ASSERT(run_ok(
    "def i64 a 1000000\n"
    "def i64 b 1000000\n"
    "[print [to-string [* $a $b]]]",
    &cap, "1000000000000\n"));
  TEST_PASS();
}

static int test_u32_dynamic_dispatch_add(void) {
  PrintCapture cap;
  /* u32 in dynamic dispatch (OP_ADD) */
  ASSERT(run_ok(
    "def u32 a 100\n"
    "def u32 b 200\n"
    "[print [to-string [+ $a $b]]]",
    &cap, "300\n"));
  TEST_PASS();
}

static int test_multiple_typed_conversions(void) {
  PrintCapture cap;
  /* Chain: i32 -> i64 -> f64 -> dyn */
  ASSERT(run_ok(
    "def i32 n 42\n"
    "def i64 big [to i64 $n]\n"
    "def f64 flt [to f64 $big]\n"
    "def d [to dyn $flt]\n"
    "[print $d]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_proc_chain_typed(void) {
  PrintCapture cap;
  /* Typed proc calling another typed proc — return type propagates */
  ASSERT(run_ok(
    "proc i64 dbl {i64 n} { * $n 2 }\n"
    "proc i64 quad {i64 n} { dbl [dbl $n] }\n"
    "[print [to-string [quad 5]]]",
    &cap, "20\n"));
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-001: Value representation */
    { "val_u32_roundtrip",              test_val_u32_roundtrip },
    { "val_u32_zero",                   test_val_u32_zero },
    { "val_u32_max",                    test_val_u32_max },
    { "val_i64_roundtrip",              test_val_i64_roundtrip },
    { "val_i64_negative",              test_val_i64_negative },
    { "val_u64_roundtrip",              test_val_u64_roundtrip },
    { "val_f64_roundtrip",              test_val_f64_roundtrip },
    { "val_predicates_exclusive",       test_val_predicates_exclusive },
    /* US-006: Typed def for all numeric types */
    { "def_i64_basic",                  test_def_i64_basic },
    { "def_u32_basic",                  test_def_u32_basic },
    { "def_u64_basic",                  test_def_u64_basic },
    { "def_f64_basic",                  test_def_f64_basic },
    { "def_i32_basic",                  test_def_i32_basic },
    { "def_f32_basic",                  test_def_f32_basic },
    { "def_untyped_unchanged",          test_def_untyped_unchanged },
    { "def_typed_error_str_to_i64",     test_def_typed_error_str_to_i64 },
    { "def_typed_error_str_to_f64",     test_def_typed_error_str_to_f64 },
    { "def_i64_local",                  test_def_i64_local },
    { "def_f64_local",                  test_def_f64_local },
    /* US-007: Typed mut and set! */
    { "mut_i64_basic",                  test_mut_i64_basic },
    { "mut_f64_basic",                  test_mut_f64_basic },
    { "mut_u64_basic",                  test_mut_u64_basic },
    { "mut_u32_basic",                  test_mut_u32_basic },
    { "set_type_error_str_to_i64",      test_set_type_error_str_to_i64 },
    { "set_type_error_dyn_to_i64",      test_set_type_error_dyn_to_i64 },
    { "set_typed_to_typed_ok",          test_set_typed_to_typed_ok },
    { "set_untyped_mut_no_checking",    test_set_untyped_mut_no_checking },
    { "mut_i64_local",                  test_mut_i64_local },
    /* US-008: Typed arithmetic */
    { "arith_i64_add",                  test_arith_i64_add },
    { "arith_i64_sub",                  test_arith_i64_sub },
    { "arith_i64_mul",                  test_arith_i64_mul },
    { "arith_i64_div",                  test_arith_i64_div },
    { "arith_i64_mod",                  test_arith_i64_mod },
    { "arith_f64_add",                  test_arith_f64_add },
    { "arith_f64_mul",                  test_arith_f64_mul },
    { "arith_f64_div",                  test_arith_f64_div },
    { "arith_u64_div",                  test_arith_u64_div },
    { "arith_u64_lt",                   test_arith_u64_lt },
    { "arith_i64_comparison",           test_arith_i64_comparison },
    { "arith_type_mismatch_i64_f64",    test_arith_type_mismatch_i64_f64 },
    { "arith_type_mismatch_i64_dyn",    test_arith_type_mismatch_i64_dyn },
    { "arith_contextual_literal",       test_arith_contextual_literal },
    { "arith_nested_i64",               test_arith_nested_i64 },
    { "arith_nested_f64",               test_arith_nested_f64 },
    { "arith_i64_neg",                  test_arith_i64_neg },
    { "arith_i64_div_by_zero",          test_arith_i64_div_by_zero },
    { "arith_f64_div_by_zero",          test_arith_f64_div_by_zero },
    { "arith_result_type_inference",    test_arith_result_type_inference },
    /* US-009: to builtin for type conversions */
    { "to_i64_from_i32",               test_to_i64_from_i32 },
    { "to_i64_from_u32",               test_to_i64_from_u32 },
    { "to_f64_from_i64",               test_to_f64_from_i64 },
    { "to_i64_from_f64",               test_to_i64_from_f64 },
    { "to_dyn_from_i64",               test_to_dyn_from_i64 },
    { "to_i64_from_dyn",               test_to_i64_from_dyn },
    { "to_dyn_dyn_noop",               test_to_dyn_dyn_noop },
    { "to_u32_from_i32",               test_to_u32_from_i32 },
    { "to_f32_from_i32",               test_to_f32_from_i32 },
    { "to_error_str_to_i64",           test_to_error_str_to_i64 },
    { "to_arity_error",                test_to_arity_error },
    /* US-010: Typed proc */
    { "proc_i64_return",               test_proc_i64_return },
    { "proc_f64_return",               test_proc_f64_return },
    { "proc_callsite_contextual",      test_proc_callsite_contextual_typing },
    { "proc_callsite_type_error",      test_proc_callsite_type_error },
    { "proc_callsite_dyn_error",       test_proc_callsite_dyn_error },
    { "proc_return_type_error",        test_proc_return_type_error },
    { "proc_return_type_at_callsite",  test_proc_return_type_at_callsite },
    { "proc_mixed_params",             test_proc_mixed_params },
    { "proc_untyped_unchanged",        test_proc_untyped_unchanged },
    /* Contextual typing */
    { "ctx_i64_literal_in_def",        test_contextual_i64_literal_in_def },
    { "ctx_f64_literal_in_def",        test_contextual_f64_literal_in_def },
    { "ctx_literal_in_set",            test_contextual_literal_in_set },
    /* Type inference */
    { "infer_from_typed_var",          test_type_inference_from_typed_var },
    { "infer_from_typed_proc",         test_type_inference_from_typed_proc },
    /* Typed closures */
    { "closure_typed_capture",         test_typed_closure_capture },
    { "closure_typed_param",           test_typed_closure_param },
    { "closure_typed_transitive",      test_typed_upvalue_transitive },
    /* US-011: Print/to-string */
    { "print_i64",                     test_print_i64 },
    { "print_u32",                     test_print_u32 },
    { "print_u64",                     test_print_u64 },
    { "print_f64",                     test_print_f64 },
    { "print_string_interpolation",    test_print_string_interpolation_typed },
    { "print_no_regression_i32",       test_print_no_regression_i32 },
    { "print_no_regression_f32",       test_print_no_regression_f32 },
    /* Interaction with existing features */
    { "typed_in_try",                  test_typed_in_try },
    { "typed_to_dyn_for_collection",   test_typed_to_dyn_for_collection },
    { "typed_if_branch",               test_typed_if_branch },
    { "typed_while_loop",              test_typed_while_loop },
    /* Edge cases */
    { "i64_large_value",               test_i64_large_value },
    { "u32_dynamic_dispatch_add",      test_u32_dynamic_dispatch_add },
    { "multiple_typed_conversions",    test_multiple_typed_conversions },
    { "proc_chain_typed",              test_proc_chain_typed },
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
