#include "test_helpers.h"
#include "../src/jacl.h"
#define TEST_CAPTURE_BUF_SIZE 8192
#include "test_run_helpers.h"

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
  ASSERT(run_err(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2]]\n"
    "[print [vec-get $points 99]]",
    "out of bounds"));
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

static int test_typed_vec_get_inline_local(void) {
  PrintCapture cap;
  /* def inside proc triggers inline path: OP_TYPED_VEC_GET_INLINE
   * instead of OP_TYPED_VEC_GET + OP_STRUCT_STORE_INLINE */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc test {} {\n"
    "  def points [[Vec Point] [Point 10 20] [Point 30 40]]\n"
    "  def p [vec-get $points 0]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "  def q [vec-get $points 1]\n"
    "  [print $q->x]\n"
    "  [print $q->y]\n"
    "}\n"
    "[test]",
    &cap, "10\n20\n30\n40\n"));
  TEST_PASS();
}

static int test_typed_vec_get_inline_wide(void) {
  PrintCapture cap;
  /* Wide struct (4 fields = 2 slots) via inline get in local scope */
  ASSERT(run_ok(
    "struct Rect {i32 x, i32 y, i32 w, i32 h}\n"
    "proc test {} {\n"
    "  def rects [[Vec Rect] [Rect 1 2 3 4] [Rect 5 6 7 8]]\n"
    "  def r [vec-get $rects 0]\n"
    "  [print $r->x]\n"
    "  [print $r->h]\n"
    "  def s [vec-get $rects 1]\n"
    "  [print $s->y]\n"
    "  [print $s->w]\n"
    "}\n"
    "[test]",
    &cap, "1\n4\n6\n7\n"));
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

/* ===== Typed Vec: Typed proc parameters ===== */

static int test_typed_vec_proc_param(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc get-len {[Vec Point] pts} { [vec-len $pts] }\n"
    "def points [[Vec Point] [Point 1 2] [Point 3 4]]\n"
    "[print [get-len $points]]",
    &cap, "2\n"));
  TEST_PASS();
}

static int test_typed_vec_proc_param_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc first-x {[Vec Point] pts} {\n"
    "  def p [vec-get $pts 0]\n"
    "  $p->x\n"
    "}\n"
    "def points [[Vec Point] [Point 42 99]]\n"
    "[print [first-x $points]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_typed_vec_proc_param_reject_dyn(void) {
  /* Passing a dyn value to a typed collection param should error */
  ASSERT(run_err(
    "struct Point {i32 x, i32 y}\n"
    "proc get-len {[Vec Point] pts} { [vec-len $pts] }\n"
    "def v [vec 1 2 3]\n"
    "[get-len $v]",
    "type error"));
  TEST_PASS();
}

/* ===== Typed Vec: For loop iteration ===== */

static int test_typed_vec_for_loop(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 10 20] [Point 30 40]]\n"
    "for $points p {\n"
    "  [print $p->x]\n"
    "}",
    &cap, "10\n30\n"));
  TEST_PASS();
}

static int test_typed_vec_for_loop_field_access(void) {
  PrintCapture cap;
  /* Print each element's y field */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2] [Point 3 4] [Point 5 6]]\n"
    "for $points p {\n"
    "  [print $p->y]\n"
    "}",
    &cap, "2\n4\n6\n"));
  TEST_PASS();
}

static int test_typed_vec_for_loop_wide_struct(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Rect {i32 x, i32 y, i32 w, i32 h}\n"
    "def rects [[Vec Rect] [Rect 1 2 3 4] [Rect 5 6 7 8]]\n"
    "for $rects r {\n"
    "  [print $r->w]\n"
    "}",
    &cap, "3\n7\n"));
  TEST_PASS();
}

static int test_typed_vec_for_loop_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point]]\n"
    "for $points p {\n"
    "  [print $p->x]\n"
    "}\n"
    "[print done]",
    &cap, "done\n"));
  TEST_PASS();
}

static int test_typed_vec_for_loop_accumulate(void) {
  PrintCapture cap;
  /* Accumulate sum of y fields using mut + set inside for loop */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 10] [Point 2 20] [Point 3 30]]\n"
    "mut sum 0\n"
    "for $points p {\n"
    "  set sum [+ $sum $p->y]\n"
    "}\n"
    "[print $sum]",
    &cap, "60\n"));
  TEST_PASS();
}

static int test_typed_vec_for_loop_break(void) {
  PrintCapture cap;
  /* Break inside typed vec for-loop: OP_CLOSE_LOOP must clean up
   * inline padding locals and bitmap bits correctly. */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 10] [Point 2 20] [Point 3 30]]\n"
    "def result [for $points p {\n"
    "  if [== $p->x 2] { [break $p->y] }\n"
    "}]\n"
    "[print $result]",
    &cap, "20\n"));
  TEST_PASS();
}

static int test_typed_vec_for_loop_break_wide(void) {
  PrintCapture cap;
  /* Break with wide struct (2 slots) — padding locals must be cleaned up */
  ASSERT(run_ok(
    "struct Rect {i32 x, i32 y, i32 w, i32 h}\n"
    "def rects [[Vec Rect] [Rect 1 2 3 4] [Rect 5 6 7 8] [Rect 9 10 11 12]]\n"
    "def result [for $rects r {\n"
    "  if [== $r->w 7] { [break $r->h] }\n"
    "}]\n"
    "[print $result]",
    &cap, "8\n"));
  TEST_PASS();
}

/* ===== Typed Vec: Box round-trip ===== */

static int test_typed_vec_box_roundtrip(void) {
  PrintCapture cap;
  /* box?/unbox flow typing requires locals, so wrap in proc */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc test {} {\n"
    "  def points [[Vec Point] [Point 10 20] [Point 30 40]]\n"
    "  def boxed [box $points]\n"
    "  if [box? [Vec Point] $boxed] {\n"
    "    def pts [unbox $boxed]\n"
    "    [print [vec-len $pts]]\n"
    "    def p [vec-get $pts 0]\n"
    "    [print $p->x]\n"
    "    [print $p->y]\n"
    "  } { [print nil] }\n"
    "}\n"
    "[test]",
    &cap, "2\n10\n20\n"));
  TEST_PASS();
}

static int test_typed_vec_box_wrong_type(void) {
  PrintCapture cap;
  /* box? [Map Point] should return false for a boxed typed vec */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc test {} {\n"
    "  def points [[Vec Point] [Point 1 2]]\n"
    "  def boxed [box $points]\n"
    "  if [box? [Map Point] $boxed] {\n"
    "    [print yes]\n"
    "  } { [print no] }\n"
    "}\n"
    "[test]",
    &cap, "no\n"));
  TEST_PASS();
}

static int test_typed_vec_box_in_dyn_vec(void) {
  PrintCapture cap;
  /* boxed typed vec CAN be stored in a dyn vec */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def points [[Vec Point] [Point 1 2]]\n"
    "def v [vec [box $points]]\n"
    "[print [vec-len $v]]",
    &cap, "1\n"));
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
  RUN(test_typed_vec_get_inline_local);
  RUN(test_typed_vec_get_inline_wide);
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
  RUN(test_typed_vec_proc_param);
  RUN(test_typed_vec_proc_param_get);
  RUN(test_typed_vec_proc_param_reject_dyn);
  RUN(test_typed_vec_for_loop);
  RUN(test_typed_vec_for_loop_field_access);
  RUN(test_typed_vec_for_loop_wide_struct);
  RUN(test_typed_vec_for_loop_empty);
  RUN(test_typed_vec_for_loop_accumulate);
  RUN(test_typed_vec_for_loop_break);
  RUN(test_typed_vec_for_loop_break_wide);
  RUN(test_typed_vec_box_roundtrip);
  RUN(test_typed_vec_box_wrong_type);
  RUN(test_typed_vec_box_in_dyn_vec);

  printf("\n%d/%d passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
