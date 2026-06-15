#include "test_helpers.h"
#include "../src/jacl.h"
#define TEST_CAPTURE_BUF_SIZE 8192
#include "test_run_helpers.h"

/* ===== Typed Map: Construction ===== */

static int test_typed_map_construct_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [map-len [[Map dyn Point]]]]",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_typed_map_construct_elements(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [map-len [[Map dyn Point] \"a\" [Point x 1 y 2] \"b\" [Point x 3 y 4]]]]",
    &cap, "2\n"));
  TEST_PASS();
}

/* ===== Typed Map: Get ===== */

static int test_typed_map_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def m [[Map dyn Point] \"a\" [Point x 10 y 20] \"b\" [Point x 30 y 40]]\n"
    "  def p [map-get $m \"a\"]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "  def q [map-get $m \"b\"]\n"
    "  [print $q->x]\n"
    "  [print $q->y]\n"
    "}\n"
    "main",
    &cap, "10\n20\n30\n40\n"));
  TEST_PASS();
}

static int test_typed_map_get_missing(void) {
  ASSERT(run_err(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "[print [map-get $m \"zzz\"]]",
    "key not found"));
  TEST_PASS();
}

static int test_typed_map_get_inline_local(void) {
  PrintCapture cap;
  /* def inside proc triggers inline path: OP_TYPED_MAP_GET_INLINE */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc test {} {\n"
    "  def m [[Map dyn Point] \"a\" [Point x 10 y 20] \"b\" [Point x 30 y 40]]\n"
    "  def p [map-get $m \"a\"]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "  def q [map-get $m \"b\"]\n"
    "  [print $q->x]\n"
    "  [print $q->y]\n"
    "}\n"
    "[test]",
    &cap, "10\n20\n30\n40\n"));
  TEST_PASS();
}

static int test_typed_map_get_inline_wide(void) {
  PrintCapture cap;
  /* Wide struct (4 fields = 2 slots) via inline map get */
  ASSERT(run_ok(
    "struct Rect {i32 x, i32 y, i32 w, i32 h}\n"
    "proc test {} {\n"
    "  def m [[Map dyn Rect] \"r\" [Rect x 1 y 2 w 3 h 4]]\n"
    "  def r [map-get $m \"r\"]\n"
    "  [print $r->x]\n"
    "  [print $r->h]\n"
    "}\n"
    "[test]",
    &cap, "1\n4\n"));
  TEST_PASS();
}

/* ===== Typed Map: Has ===== */

static int test_typed_map_has(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point] \"x\" [Point x 1 y 2]]\n"
    "[print [map-has $m \"x\"]]\n"
    "[print [map-has $m \"y\"]]",
    &cap, "true\nfalse\n"));
  TEST_PASS();
}

/* ===== Typed Map: Set ===== */

static int test_typed_map_set(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "  def m2 [map-set $m \"b\" [Point x 3 y 4]]\n"
    "  [print [map-len $m2]]\n"
    "  def p [map-get $m2 \"b\"]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "  # original unchanged (persistent)\n"
    "  [print [map-len $m]]\n"
    "}\n"
    "main",
    &cap, "2\n3\n4\n1\n"));
  TEST_PASS();
}

static int test_typed_map_set_overwrite(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "  def m2 [map-set $m \"a\" [Point x 99 y 88]]\n"
    "  [print [map-len $m2]]\n"
    "  def p [map-get $m2 \"a\"]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "}\n"
    "main",
    &cap, "1\n99\n88\n"));
  TEST_PASS();
}

/* ===== Typed Map: Remove ===== */

static int test_typed_map_remove(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2] \"b\" [Point x 3 y 4]]\n"
    "def m2 [map-remove $m \"a\"]\n"
    "[print [map-len $m2]]\n"
    "[print [map-has $m2 \"a\"]]\n"
    "[print [map-has $m2 \"b\"]]\n"
    "# original unchanged\n"
    "[print [map-len $m]]",
    &cap, "1\nfalse\ntrue\n2\n"));
  TEST_PASS();
}

/* ===== Typed Map: Len ===== */

static int test_typed_map_len(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [map-len [[Map dyn Point]]]]\n"
    "[print [map-len [[Map dyn Point] \"a\" [Point x 1 y 2]]]]\n"
    "[print [map-len [[Map dyn Point] \"a\" [Point x 1 y 2] \"b\" [Point x 3 y 4] \"c\" [Point x 5 y 6]]]]",
    &cap, "0\n1\n3\n"));
  TEST_PASS();
}

/* ===== Typed Map: Keys ===== */

static int test_typed_map_keys(void) {
  PrintCapture cap;
  /* keys returns a dyn vec — test length */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point] \"x\" [Point x 1 y 2] \"y\" [Point x 3 y 4]]\n"
    "[print [vec-len [map-keys $m]]]",
    &cap, "2\n"));
  TEST_PASS();
}

/* ===== Typed Map: Vals ===== */

static int test_typed_map_vals(void) {
  PrintCapture cap;
  /* vals returns a typed vec of structs */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def m [[Map dyn Point] \"a\" [Point x 10 y 20]]\n"
    "  def vals [map-vals $m]\n"
    "  [print [vec-len $vals]]\n"
    "  def p [vec-get $vals 0]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "}\n"
    "main",
    &cap, "1\n10\n20\n"));
  TEST_PASS();
}

/* ===== Typed Map: Wide struct (4 fields) ===== */

static int test_typed_map_wide_struct(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Rect {i32 x, i32 y, i32 w, i32 h}\n"
    "proc main {} {\n"
    "  def m [[Map dyn Rect] \"a\" [Rect x 10 y 20 w 100 h 200] \"b\" [Rect x 30 y 40 w 300 h 400]]\n"
    "  [print [map-len $m]]\n"
    "  def r [map-get $m \"a\"]\n"
    "  [print $r->x]\n"
    "  [print $r->w]\n"
    "  def s [map-get $m \"b\"]\n"
    "  [print $s->y]\n"
    "  [print $s->h]\n"
    "}\n"
    "main",
    &cap, "2\n10\n100\n40\n400\n"));
  TEST_PASS();
}

/* ===== Typed Map: Persistence ===== */

static int test_typed_map_persistence(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Map dyn Point] \"k\" [Point x 1 y 2]]\n"
    "def b [map-set $a \"k2\" [Point x 3 y 4]]\n"
    "# a should still have length 1\n"
    "[print [map-len $a]]\n"
    "[print [map-len $b]]",
    &cap, "1\n2\n"));
  TEST_PASS();
}

/* ===== Typed Map: Integer keys ===== */

static int test_typed_map_integer_keys(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def m [[Map dyn Point] 42 [Point x 1 y 2] 99 [Point x 3 y 4]]\n"
    "  def p [map-get $m 42]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "  [print [map-has $m 99]]\n"
    "  [print [map-has $m 0]]\n"
    "}\n"
    "main",
    &cap, "1\n2\ntrue\nfalse\n"));
  TEST_PASS();
}

/* ===== Typed Map: GC safety ===== */

static int test_typed_map_gc_safety(void) {
  PrintCapture cap;
  /* Chain set operations to allocate many nodes, exercising GC */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  mut m [[Map dyn Point]]\n"
    "  set m [map-set $m \"a\" [Point x 1 y 2]]\n"
    "  set m [map-set $m \"b\" [Point x 3 y 4]]\n"
    "  set m [map-set $m \"c\" [Point x 5 y 6]]\n"
    "  set m [map-set $m \"d\" [Point x 7 y 8]]\n"
    "  set m [map-set $m \"e\" [Point x 9 y 10]]\n"
    "  [print [map-len $m]]\n"
    "  def p [map-get $m \"a\"]\n"
    "  [print $p->x]\n"
    "  def q [map-get $m \"e\"]\n"
    "  [print $q->y]\n"
    "}\n"
    "main",
    &cap, "5\n1\n10\n"));
  TEST_PASS();
}

/* ===== Typed Map: Closure capture (upvalue path) ===== */

static int test_typed_map_closure_capture(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc make-lookup {} {\n"
    "  def m [[Map dyn Point] \"origin\" [Point x 0 y 0]]\n"
    "  proc count {} { [map-len $m] }\n"
    "  $count\n"
    "}\n"
    "def counter [make-lookup]\n"
    "[print [counter]]",
    &cap, "1\n"));
  TEST_PASS();
}

/* ===== Typed Map: Print formatting ===== */

static int test_typed_map_print(void) {
  PrintCapture cap;
  /* Single entry — deterministic iteration order */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "[print $m]",
    &cap, "[[Map dyn Point] \"a\" [Point x 1 y 2]]\n"));
  TEST_PASS();
}

static int test_typed_map_print_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [[Map dyn Point]]]",
    &cap, "[[Map dyn Point]]\n"));
  TEST_PASS();
}

/* ===== Typed Map: Equality ===== */

static int test_typed_map_eq_same(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Map dyn Point] \"k\" [Point x 1 y 2]]\n"
    "def b [[Map dyn Point] \"k\" [Point x 1 y 2]]\n"
    "[print [== $a $b]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_typed_map_eq_different_value(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Map dyn Point] \"k\" [Point x 1 y 2]]\n"
    "def b [[Map dyn Point] \"k\" [Point x 3 y 4]]\n"
    "[print [== $a $b]]",
    &cap, "false\n"));
  TEST_PASS();
}

static int test_typed_map_eq_different_key(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Map dyn Point] \"x\" [Point x 1 y 2]]\n"
    "def b [[Map dyn Point] \"y\" [Point x 1 y 2]]\n"
    "[print [== $a $b]]",
    &cap, "false\n"));
  TEST_PASS();
}

static int test_typed_map_eq_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Map dyn Point]]\n"
    "def b [[Map dyn Point]]\n"
    "[print [== $a $b]]",
    &cap, "true\n"));
  TEST_PASS();
}

/* ===== Typed Map: Boxing invariant ===== */

static int test_typed_map_reject_in_dyn_vec(void) {
  ASSERT(run_err(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "[vec $m]",
    "cannot store bare"));
  TEST_PASS();
}

static int test_typed_map_reject_dyn_proc_param(void) {
  ASSERT(run_err(
    "struct Point {i32 x, i32 y}\n"
    "proc show-len {m} { [print [map-len $m]] }\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "[show-len $m]",
    "cannot pass bare"));
  TEST_PASS();
}

/* ===== Typed Map: Typed proc parameters ===== */

static int test_typed_map_proc_param(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc get-len {[Map dyn Point] m} { [map-len $m] }\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2] \"b\" [Point x 3 y 4]]\n"
    "[print [get-len $m]]",
    &cap, "2\n"));
  TEST_PASS();
}

static int test_typed_map_proc_param_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc lookup-x {[Map dyn Point] m} {\n"
    "  def p [map-get $m \"a\"]\n"
    "  $p->x\n"
    "}\n"
    "def m [[Map dyn Point] \"a\" [Point x 42 y 99]]\n"
    "[print [lookup-x $m]]",
    &cap, "42\n"));
  TEST_PASS();
}

/* ===== Typed Map: Box round-trip ===== */

static int test_typed_map_box_roundtrip(void) {
  PrintCapture cap;
  /* box?/unbox flow typing requires locals, so wrap in proc */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc test {} {\n"
    "  def m [[Map dyn Point] \"a\" [Point x 10 y 20] \"b\" [Point x 30 y 40]]\n"
    "  def boxed [box $m]\n"
    "  if [box? [Map dyn Point] $boxed] {\n"
    "    def m2 [unbox $boxed]\n"
    "    [print [map-len $m2]]\n"
    "    def p [map-get $m2 \"a\"]\n"
    "    [print $p->x]\n"
    "    [print $p->y]\n"
    "  } { [print nil] }\n"
    "}\n"
    "[test]",
    &cap, "2\n10\n20\n"));
  TEST_PASS();
}

static int test_typed_map_box_wrong_type(void) {
  PrintCapture cap;
  /* box? [Vec Point] should return false for a boxed typed map */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc test {} {\n"
    "  def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "  def boxed [box $m]\n"
    "  if [box? [Vec Point] $boxed] {\n"
    "    [print yes]\n"
    "  } { [print no] }\n"
    "}\n"
    "[test]",
    &cap, "no\n"));
  TEST_PASS();
}

static int test_typed_map_box_in_dyn_vec(void) {
  PrintCapture cap;
  /* boxed typed map CAN be stored in a dyn vec */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "def v [vec [box $m]]\n"
    "[print [vec-len $v]]",
    &cap, "1\n"));
  TEST_PASS();
}

/* ===== Typed Map: Each (HOF via for) ===== */

static int test_typed_map_each_hof(void) {
  PrintCapture cap;
  /* HOF form of for with typed map — callback receives key + materialized struct */
  /* Sum values to avoid order-dependence */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point] \"a\" [Point x 10 y 20] \"b\" [Point x 30 y 40]]\n"
    "mut total 0\n"
    "proc accum {k Point v} { set total [+ $total $v->x] }\n"
    "[for $m $accum]\n"
    "[print $total]",
    &cap, "40\n"));
  TEST_PASS();
}

static int test_typed_map_each_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map dyn Point]]\n"
    "proc noop {k Point v} { }\n"
    "[for $m $noop]\n"
    "[print done]",
    &cap, "done\n"));
  TEST_PASS();
}

/* ===== Typed Map: Transform (HOF) ===== */

static int test_typed_map_transform(void) {
  PrintCapture cap;
  /* Transform typed map — callback extracts x field */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc getx {k Point v} { $v->x }\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2] \"b\" [Point x 3 y 4]]\n"
    "def xs [transform $m $getx]\n"
    "[print [vec-len $xs]]",
    &cap, "2\n"));
  TEST_PASS();
}

/* ===== Typed Map: Filter (HOF) ===== */

static int test_typed_map_filter(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc isbig {k Point v} { [> $v->x 10] }\n"
    "proc main {} {\n"
    "  def m [[Map dyn Point] \"a\" [Point x 1 y 2] \"b\" [Point x 30 y 40] \"c\" [Point x 5 y 6]]\n"
    "  def big [filter $m $isbig]\n"
    "  [print [map-len $big]]\n"
    "  def p [map-get $big \"b\"]\n"
    "  [print $p->x]\n"
    "}\n"
    "main",
    &cap, "1\n30\n"));
  TEST_PASS();
}

static int test_typed_map_filter_none(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc ishuge {k Point v} { [> $v->x 99] }\n"
    "def m [[Map dyn Point] \"a\" [Point x 1 y 2]]\n"
    "def result [filter $m $ishuge]\n"
    "[print [map-len $result]]",
    &cap, "0\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Construction ===== */

static int test_skey_construct_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "[print [map-len [[Map Id Point]]]]\n",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_skey_construct_elements(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "[print [map-len [[Map Id Point] [Id n 1] [Point x 10 y 20] [Id n 2] [Point x 30 y 40]]]]\n",
    &cap, "2\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Get ===== */

static int test_skey_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def m [[Map Id Point] [Id n 1] [Point x 10 y 20] [Id n 2] [Point x 30 y 40]]\n"
    "  def p [map-get $m [Id n 1]]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "  def q [map-get $m [Id n 2]]\n"
    "  [print $q->x]\n"
    "  [print $q->y]\n"
    "}\n"
    "main\n",
    &cap, "10\n20\n30\n40\n"));
  TEST_PASS();
}

static int test_skey_get_missing(void) {
  ASSERT(run_err(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Id Point] [Id n 1] [Point x 10 y 20]]\n"
    "[map-get $m [Id n 99]]\n",
    "key not found"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Has ===== */

static int test_skey_has(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Id Point] [Id n 42] [Point x 1 y 2]]\n"
    "[print [map-has $m [Id n 42]]]\n"
    "[print [map-has $m [Id n 99]]]\n",
    &cap, "true\nfalse\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Set ===== */

static int test_skey_set(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def m [[Map Id Point] [Id n 1] [Point x 10 y 20]]\n"
    "  def m2 [map-set $m [Id n 2] [Point x 30 y 40]]\n"
    "  [print [map-len $m2]]\n"
    "  def p [map-get $m2 [Id n 2]]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "  # original unchanged\n"
    "  [print [map-len $m]]\n"
    "}\n"
    "main\n",
    &cap, "2\n30\n40\n1\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Remove ===== */

static int test_skey_remove(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Id Point] [Id n 1] [Point x 10 y 20] [Id n 2] [Point x 30 y 40]]\n"
    "def m2 [map-remove $m [Id n 1]]\n"
    "[print [map-len $m2]]\n"
    "[print [map-has $m2 [Id n 1]]]\n"
    "[print [map-has $m2 [Id n 2]]]\n",
    &cap, "1\nfalse\ntrue\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Len ===== */

static int test_skey_len(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "[print [map-len [[Map Id Point]]]]\n"
    "[print [map-len [[Map Id Point] [Id n 1] [Point x 1 y 2]]]]\n"
    "[print [map-len [[Map Id Point] [Id n 1] [Point x 1 y 2] [Id n 2] [Point x 3 y 4] [Id n 3] [Point x 5 y 6]]]]\n",
    &cap, "0\n1\n3\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Keys ===== */

static int test_skey_keys(void) {
  PrintCapture cap;
  /* struct keys: map-keys returns typed vec */
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Id Point] [Id n 1] [Point x 10 y 20] [Id n 2] [Point x 30 y 40]]\n"
    "def ks [map-keys $m]\n"
    "[print [vec-len $ks]]\n",
    &cap, "2\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Vals ===== */

static int test_skey_vals(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def m [[Map Id Point] [Id n 1] [Point x 10 y 20]]\n"
    "  def vs [map-vals $m]\n"
    "  [print [vec-len $vs]]\n"
    "  def p [vec-get $vs 0]\n"
    "  [print $p->x]\n"
    "  [print $p->y]\n"
    "}\n"
    "main\n",
    &cap, "1\n10\n20\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Equality ===== */

static int test_skey_eq_same(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "def a [[Map Id Point] [Id n 1] [Point x 10 y 20]]\n"
    "def b [[Map Id Point] [Id n 1] [Point x 10 y 20]]\n"
    "[print [== $a $b]]\n",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_skey_eq_different(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "def a [[Map Id Point] [Id n 1] [Point x 10 y 20]]\n"
    "def b [[Map Id Point] [Id n 1] [Point x 99 y 99]]\n"
    "[print [== $a $b]]\n",
    &cap, "false\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Print ===== */

static int test_skey_print(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Id Point] [Id n 42] [Point x 1 y 2]]\n"
    "[print $m]\n",
    &cap, "[[Map Id Point] [Id n 42] [Point x 1 y 2]]\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Proc param ===== */

static int test_skey_proc_param(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "proc get-len {[Map Id Point] m} { [map-len $m] }\n"
    "def m [[Map Id Point] [Id n 1] [Point x 10 y 20] [Id n 2] [Point x 30 y 40]]\n"
    "[print [get-len $m]]\n",
    &cap, "2\n"));
  TEST_PASS();
}

static int test_skey_proc_param_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "proc lookup {[Map Id Point] m Id k} {\n"
    "  def p [map-get $m $k]\n"
    "  $p->x\n"
    "}\n"
    "def m [[Map Id Point] [Id n 1] [Point x 42 y 99]]\n"
    "[print [lookup $m [Id n 1]]]\n",
    &cap, "42\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Each HOF ===== */

static int test_skey_each(void) {
  PrintCapture cap;
  /* Sum values to avoid order dependence */
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Id Point] [Id n 1] [Point x 10 y 20] [Id n 2] [Point x 30 y 40]]\n"
    "mut total 0\n"
    "proc accum {Id k Point v} { set total [+ $total $v->x] }\n"
    "[for $m $accum]\n"
    "[print $total]\n",
    &cap, "40\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Filter HOF ===== */

static int test_skey_filter(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "proc isbig {Id k Point v} { [> $v->x 10] }\n"
    "proc main {} {\n"
    "  def m [[Map Id Point] [Id n 1] [Point x 1 y 2] [Id n 2] [Point x 30 y 40]]\n"
    "  def big [filter $m $isbig]\n"
    "  [print [map-len $big]]\n"
    "  def p [map-get $big [Id n 2]]\n"
    "  [print $p->x]\n"
    "}\n"
    "main\n",
    &cap, "1\n30\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: GC safety ===== */

static int test_skey_gc_safety(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Id {i32 n}\n"
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  mut m [[Map Id Point]]\n"
    "  set m [map-set $m [Id n 1] [Point x 1 y 2]]\n"
    "  set m [map-set $m [Id n 2] [Point x 3 y 4]]\n"
    "  set m [map-set $m [Id n 3] [Point x 5 y 6]]\n"
    "  set m [map-set $m [Id n 4] [Point x 7 y 8]]\n"
    "  set m [map-set $m [Id n 5] [Point x 9 y 10]]\n"
    "  [print [map-len $m]]\n"
    "  def p [map-get $m [Id n 1]]\n"
    "  [print $p->x]\n"
    "  def q [map-get $m [Id n 5]]\n"
    "  [print $q->y]\n"
    "}\n"
    "main\n",
    &cap, "5\n1\n10\n"));
  TEST_PASS();
}

/* ===== Struct-Key Typed Map: Wide struct key ===== */

static int test_skey_wide_key(void) {
  PrintCapture cap;
  /* Key has 4 fields = 2 slots wide */
  ASSERT(run_ok(
    "struct Coord {i32 x, i32 y, i32 z, i32 w}\n"
    "struct Val {i32 n}\n"
    "proc main {} {\n"
    "  def m [[Map Coord Val] [Coord x 1 y 2 z 3 w 4] [Val n 42]]\n"
    "  [print [map-len $m]]\n"
    "  def v [map-get $m [Coord x 1 y 2 z 3 w 4]]\n"
    "  [print $v->n]\n"
    "  [print [map-has $m [Coord x 1 y 2 z 3 w 4]]]\n"
    "  [print [map-has $m [Coord x 9 y 9 z 9 w 9]]]\n"
    "}\n"
    "main\n",
    &cap, "1\n42\ntrue\nfalse\n"));
  TEST_PASS();
}

/* ===== Scalar element/key types ===== */

static int test_typed_map_scalar_value_construct(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [[Map dyn i64] \"a\" 10 \"b\" 20]\n"
    "print [to-string [map-len $m]]",
    &cap, "2\n"));
  TEST_PASS();
}

static int test_typed_map_scalar_value_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  def m [[Map dyn i64] \"a\" 10 \"b\" 20]\n"
    "  print [to-string [map-get $m \"b\"]]\n"
    "}\n"
    "[t]",
    &cap, "20\n"));
  TEST_PASS();
}

static int test_typed_map_scalar_kv(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  def m [[Map i32 i64] 1 100 2 200]\n"
    "  print [to-string [map-get $m 2]]\n"
    "}\n"
    "[t]",
    &cap, "200\n"));
  TEST_PASS();
}

static int test_typed_map_scalar_value_set(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  def m [[Map dyn i64] \"a\" 1]\n"
    "  def m2 [map-set $m \"b\" 99]\n"
    "  print [to-string [map-get $m2 \"b\"]]\n"
    "}\n"
    "[t]",
    &cap, "99\n"));
  TEST_PASS();
}

static int test_typed_map_scalar_value_has(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [[Map dyn i64] \"a\" 1]\n"
    "print [to-string [map-has $m \"a\"]]\n"
    "print [to-string [map-has $m \"z\"]]",
    &cap, "true\nfalse\n"));
  TEST_PASS();
}

static int test_typed_map_scalar_value_print(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [[Map dyn i64] \"x\" 10]\n"
    "print $m",
    &cap, "[[Map dyn i64] \"x\" 10]\n"));
  TEST_PASS();
}

static int test_typed_map_scalar_value_type_error(void) {
  ASSERT(run_err(
    "def m [[Map dyn i64] \"a\" \"oops\"]\n",
    "value 0 is not a i64"));
  TEST_PASS();
}

static int test_typed_map_ref_value_str(void) {
  /* Ref-kind value types ([Map dyn str] / [Map dyn dyn]) are legal since the
   * [Map K V] ref-kind generalization: they use the PLAIN traced map rep
   * with the value type carried statically. Construction + get work; a
   * concrete non-str value is a compile-time element error. */
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [[Map dyn str] \"a\" \"hi\"]\n"
    "print [map-get $m \"a\"]",
    &cap, "hi\n"));
  ASSERT(run_err(
    "def m [[Map dyn str] \"a\" 42]\n",
    "value 0 is not a str"));
  TEST_PASS();
}

static int test_typed_map_v_form_removed(void) {
  /* The one-arg [Map V] form was removed: maps are always [Map K V],
   * `map` is shorthand for [Map dyn dyn]. Both the ctor and the def
   * annotation emit the migration diagnostic. */
  ASSERT(run_err(
    "def m [[Map i64] \"a\" 1]\n",
    "[Map V] was removed"));
  ASSERT(run_err(
    "def [Map i64] m [map]\n",
    "[Map V] was removed"));
  TEST_PASS();
}

/* ===== Main ===== */

int main(void) {
  int pass = 0, fail = 0;

  #define RUN(fn) do { \
    printf("  %-56s ", #fn); \
    if (fn()) { pass++; } else { fail++; printf("FAIL\n"); } \
  } while (0)

  printf("=== Typed Map Tests ===\n");

  RUN(test_typed_map_construct_empty);
  RUN(test_typed_map_construct_elements);
  RUN(test_typed_map_get);
  RUN(test_typed_map_get_missing);
  RUN(test_typed_map_get_inline_local);
  RUN(test_typed_map_get_inline_wide);
  RUN(test_typed_map_has);
  RUN(test_typed_map_set);
  RUN(test_typed_map_set_overwrite);
  RUN(test_typed_map_remove);
  RUN(test_typed_map_len);
  RUN(test_typed_map_keys);
  RUN(test_typed_map_vals);
  RUN(test_typed_map_wide_struct);
  RUN(test_typed_map_persistence);
  RUN(test_typed_map_integer_keys);
  RUN(test_typed_map_gc_safety);
  RUN(test_typed_map_closure_capture);
  RUN(test_typed_map_print);
  RUN(test_typed_map_print_empty);
  RUN(test_typed_map_eq_same);
  RUN(test_typed_map_eq_different_value);
  RUN(test_typed_map_eq_different_key);
  RUN(test_typed_map_eq_empty);
  RUN(test_typed_map_reject_in_dyn_vec);
  RUN(test_typed_map_reject_dyn_proc_param);
  RUN(test_typed_map_proc_param);
  RUN(test_typed_map_proc_param_get);
  RUN(test_typed_map_box_roundtrip);
  RUN(test_typed_map_box_wrong_type);
  RUN(test_typed_map_box_in_dyn_vec);
  RUN(test_typed_map_each_hof);
  RUN(test_typed_map_each_empty);
  RUN(test_typed_map_transform);
  RUN(test_typed_map_filter);
  RUN(test_typed_map_filter_none);

  // struct-key tests
  RUN(test_skey_construct_empty);
  RUN(test_skey_construct_elements);
  RUN(test_skey_get);
  RUN(test_skey_get_missing);
  RUN(test_skey_has);
  RUN(test_skey_set);
  RUN(test_skey_remove);
  RUN(test_skey_len);
  RUN(test_skey_keys);
  RUN(test_skey_vals);
  RUN(test_skey_eq_same);
  RUN(test_skey_eq_different);
  RUN(test_skey_print);
  RUN(test_skey_proc_param);
  RUN(test_skey_proc_param_get);
  RUN(test_skey_each);
  RUN(test_skey_filter);
  RUN(test_skey_gc_safety);
  RUN(test_skey_wide_key);

  RUN(test_typed_map_scalar_value_construct);
  RUN(test_typed_map_scalar_value_get);
  RUN(test_typed_map_scalar_kv);
  RUN(test_typed_map_scalar_value_set);
  RUN(test_typed_map_scalar_value_has);
  RUN(test_typed_map_scalar_value_print);
  RUN(test_typed_map_scalar_value_type_error);
  RUN(test_typed_map_ref_value_str);
  RUN(test_typed_map_v_form_removed);

  printf("\n%d/%d passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
