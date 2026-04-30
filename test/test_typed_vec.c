#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== Print capture helper ===== */

typedef struct {
  char     buf[8192];
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

/* ===== Typed Vec: Construction ===== */

static int test_typed_vec_construct_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [vec-len [[Vec Point]]]]",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_typed_vec_construct_elements(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [vec-len [[Vec Point] [Point 1 2] [Point 3 4] [Point 5 6]]]]",
    &cap, "3\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Get ===== */

static int test_typed_vec_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 10 20] [Point 30 40]]\n"
    "def p [vec-get $points 0]\n"
    "[print $p->x]\n"
    "[print $p->y]\n"
    "def q [vec-get $points 1]\n"
    "[print $q->x]\n"
    "[print $q->y]",
    &cap, "10\n20\n30\n40\n"));
  TEST_PASS();
}

static int test_typed_vec_get_oob(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2]]\n"
    "[print [vec-get $points 99]]",
    &cap, "nil\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Push ===== */

static int test_typed_vec_push(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2]]\n"
    "def points2 [vec-push $points [Point 3 4]]\n"
    "[print [vec-len $points2]]\n"
    "def p [vec-get $points2 1]\n"
    "[print $p->x]\n"
    "[print $p->y]",
    &cap, "2\n3\n4\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Set ===== */

static int test_typed_vec_set(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2] [Point 3 4]]\n"
    "def points2 [vec-set $points 0 [Point 99 88]]\n"
    "def p [vec-get $points2 0]\n"
    "[print $p->x]\n"
    "[print $p->y]\n"
    "# original unchanged (persistent)\n"
    "def q [vec-get $points 0]\n"
    "[print $q->x]",
    &cap, "99\n88\n1\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Len ===== */

static int test_typed_vec_len(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [vec-len [[Vec Point]]]]\n"
    "[print [vec-len [[Vec Point] [Point 1 2]]]]\n"
    "[print [vec-len [[Vec Point] [Point 1 2] [Point 3 4] [Point 5 6] [Point 7 8] [Point 9 10]]]]",
    &cap, "0\n1\n5\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Get returns usable struct (field access) ===== */

static int test_typed_vec_get_field_access(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 100 200]]\n"
    "def p [vec-get $points 0]\n"
    "[print $p]\n"
    "[print $p->x]\n"
    "[print $p->y]",
    &cap, "Point{x: 100, y: 200}\n100\n200\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Multi-field struct (wider than 1 slot) ===== */

static int test_typed_vec_wide_struct(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Rect {i32 x, i32 y, i32 w, i32 h}\n"
    "def rects [[Vec Rect] [Rect 10 20 100 200] [Rect 30 40 300 400]]\n"
    "[print [vec-len $rects]]\n"
    "def r [vec-get $rects 0]\n"
    "[print $r->x]\n"
    "[print $r->w]\n"
    "def s [vec-get $rects 1]\n"
    "[print $s->y]\n"
    "[print $s->h]",
    &cap, "2\n10\n100\n40\n400\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Persistence (immutable) ===== */

static int test_typed_vec_persistence(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Vec Point] [Point 1 2]]\n"
    "def b [vec-push $a [Point 3 4]]\n"
    "# a should still have length 1\n"
    "[print [vec-len $a]]\n"
    "[print [vec-len $b]]",
    &cap, "1\n2\n"));
  TEST_PASS();
}

/* ===== Typed Vec: GC safety — allocate enough to trigger collection ===== */

static int test_typed_vec_gc_safety(void) {
  PrintCapture cap;
  /* Chained push + get: ensure type is propagated through multiple pushes */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Vec Point] [Point 1 2]]\n"
    "def b [vec-push $a [Point 3 4]]\n"
    "def c [vec-push $b [Point 5 6]]\n"
    "def d [vec-push $c [Point 7 8]]\n"
    "def e [vec-push $d [Point 9 10]]\n"
    "[print [vec-len $e]]\n"
    "def p [vec-get $e 0]\n"
    "[print $p->x]\n"
    "def q [vec-get $e 4]\n"
    "[print $q->y]",
    &cap, "5\n1\n10\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Closure capture (upvalue path) ===== */

static int test_typed_vec_closure_capture(void) {
  PrintCapture cap;
  /* Closure captures a typed vec local (not a parameter).
     The upvalue path must propagate TYPE_TYPED_VEC so vec-len
     emits OP_TYPED_VEC_LEN, not OP_VEC_LEN. */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc make-counter {} {\n"
    "  def points [[Vec Point] [Point 10 20] [Point 30 40]]\n"
    "  proc count {} { [vec-len $points] }\n"
    "  $count\n"
    "}\n"
    "def counter [make-counter]\n"
    "[print [counter]]",
    &cap, "2\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Print formatting ===== */

static int test_typed_vec_print(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2] [Point 3 4]]\n"
    "[print $points]",
    &cap, "[Point{x: 1, y: 2}, Point{x: 3, y: 4}]\n"));
  TEST_PASS();
}

static int test_typed_vec_print_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [[Vec Point]]]",
    &cap, "[]\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Equality ===== */

static int test_typed_vec_eq_same(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Vec Point] [Point 1 2] [Point 3 4]]\n"
    "def b [[Vec Point] [Point 1 2] [Point 3 4]]\n"
    "[print [== $a $b]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_typed_vec_eq_different(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Vec Point] [Point 1 2]]\n"
    "def b [[Vec Point] [Point 3 4]]\n"
    "[print [== $a $b]]",
    &cap, "false\n"));
  TEST_PASS();
}

static int test_typed_vec_eq_different_length(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Vec Point] [Point 1 2]]\n"
    "def b [[Vec Point] [Point 1 2] [Point 3 4]]\n"
    "[print [== $a $b]]",
    &cap, "false\n"));
  TEST_PASS();
}

static int test_typed_vec_eq_identity(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Vec Point] [Point 1 2]]\n"
    "[print [== $a $a]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_typed_vec_eq_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Vec Point]]\n"
    "def b [[Vec Point]]\n"
    "[print [== $a $b]]",
    &cap, "true\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Boxing invariant ===== */

static int test_typed_vec_reject_in_dyn_vec(void) {
  ASSERT(run_err(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2]]\n"
    "[vec $points]",
    "cannot store bare"));
  TEST_PASS();
}

static int test_typed_vec_reject_in_dyn_map(void) {
  ASSERT(run_err(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2]]\n"
    "[map \"k\" $points]",
    "cannot store bare"));
  TEST_PASS();
}

static int test_typed_vec_reject_dyn_proc_param(void) {
  ASSERT(run_err(
    "struct Point {i32 x, i32 y}\n"
    "proc show-len {v} { [print [vec-len $v]] }\n"
    "def points [[Vec Point] [Point 1 2]]\n"
    "[show-len $points]",
    "cannot pass bare"));
  TEST_PASS();
}

/* ===== Main ===== */

int main(void) {
  int pass = 0, fail = 0;

  #define RUN(fn) do { \
    printf("  %-56s ", #fn); \
    if (fn()) { pass++; } else { fail++; printf("FAIL\n"); } \
  } while (0)

  printf("=== Typed Vec Tests ===\n");

  RUN(test_typed_vec_construct_empty);
  RUN(test_typed_vec_construct_elements);
  RUN(test_typed_vec_get);
  RUN(test_typed_vec_get_oob);
  RUN(test_typed_vec_push);
  RUN(test_typed_vec_set);
  RUN(test_typed_vec_len);
  RUN(test_typed_vec_get_field_access);
  RUN(test_typed_vec_wide_struct);
  RUN(test_typed_vec_persistence);
  RUN(test_typed_vec_gc_safety);
  RUN(test_typed_vec_closure_capture);
  RUN(test_typed_vec_print);
  RUN(test_typed_vec_print_empty);
  RUN(test_typed_vec_eq_same);
  RUN(test_typed_vec_eq_different);
  RUN(test_typed_vec_eq_different_length);
  RUN(test_typed_vec_eq_identity);
  RUN(test_typed_vec_eq_empty);
  RUN(test_typed_vec_reject_in_dyn_vec);
  RUN(test_typed_vec_reject_in_dyn_map);
  RUN(test_typed_vec_reject_dyn_proc_param);

  printf("\n%d/%d passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
