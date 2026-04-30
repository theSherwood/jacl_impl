#include "test_helpers.h"
#include "../src/jacl.h"
#include "test_run_helpers.h"

/* ===== US-002: Vector construction and basic accessors ===== */

static int test_vec_construct_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [vec-len [vec]]]", &cap, "0\n"));
  TEST_PASS();
}

static int test_vec_construct_elements(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [vec-len [vec 1 2 3]]]", &cap, "3\n"));
  TEST_PASS();
}

static int test_vec_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 10 20 30]\n"
    "[print [vec-get $v 0]]\n"
    "[print [vec-get $v 1]]\n"
    "[print [vec-get $v 2]]",
    &cap, "10\n20\n30\n"));
  TEST_PASS();
}

static int test_vec_get_oob(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 1 2 3]\n"
    "[print [vec-get $v 99]]",
    &cap, "nil\n"));
  TEST_PASS();
}

static int test_vec_len(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [vec-len [vec]]]\n"
    "[print [vec-len [vec 1]]]\n"
    "[print [vec-len [vec 1 2 3 4 5]]]",
    &cap, "0\n1\n5\n"));
  TEST_PASS();
}

static int test_vec_mixed_types(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 42 \"hello\" $true $nil 3.14]\n"
    "[print [vec-get $v 0]]\n"
    "[print [vec-get $v 1]]\n"
    "[print [vec-get $v 2]]\n"
    "[print [vec-get $v 3]]\n"
    "[print [vec-len $v]]",
    &cap, "42\nhello\ntrue\nnil\n5\n"));
  TEST_PASS();
}

static int test_vec_arity_get(void) {
  ASSERT(run_err("[vec-get [vec]]", "builtin 'vec-get' expects 2 arguments"));
  TEST_PASS();
}

static int test_vec_arity_len(void) {
  ASSERT(run_err("[vec-len]", "builtin 'vec-len' expects 1 argument"));
  TEST_PASS();
}

/* ===== US-003: Vector update operations ===== */

static int test_vec_push(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 1 2 3]\n"
    "def v2 [vec-push $v 4]\n"
    "[print [vec-len $v2]]\n"
    "[print [vec-get $v2 3]]",
    &cap, "4\n4\n"));
  TEST_PASS();
}

static int test_vec_set(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 1 2 3]\n"
    "def v2 [vec-set $v 1 \"x\"]\n"
    "[print [vec-get $v2 1]]",
    &cap, "x\n"));
  TEST_PASS();
}

static int test_vec_set_oob(void) {
  PrintCapture cap;
  /* OOB grows vector with nil fill */
  ASSERT(run_ok(
    "def v [vec 1 2 3]\n"
    "[print [vec-set $v 5 \"x\"]]",
    &cap, "[vec 1 2 3 nil nil \"x\"]\n"));
  TEST_PASS();
}

static int test_vec_set_empty(void) {
  PrintCapture cap;
  /* vec-set on empty vector at index 0 */
  ASSERT(run_ok(
    "[print [vec-set [vec] 0 \"x\"]]",
    &cap, "[vec \"x\"]\n"));
  TEST_PASS();
}

static int test_vec_set_empty_gap(void) {
  PrintCapture cap;
  /* vec-set on empty vector with gap fill */
  ASSERT(run_ok(
    "[print [vec-set [vec] 3 \"x\"]]",
    &cap, "[vec nil nil nil \"x\"]\n"));
  TEST_PASS();
}

static int test_vec_set_neg(void) {
  ASSERT(run_err(
    "def v [vec 1 2 3]\n"
    "def i [- 0 1]\n"
    "[vec-set $v $i \"x\"]",
    "vec-set: negative index -1"));
  TEST_PASS();
}

static int test_vec_set_oob_persist(void) {
  PrintCapture cap;
  /* Original unchanged after OOB vec-set */
  ASSERT(run_ok(
    "def v [vec 1 2 3]\n"
    "def v2 [vec-set $v 5 \"x\"]\n"
    "[print [vec-len $v]]\n"
    "[print [vec-len $v2]]",
    &cap, "3\n6\n"));
  TEST_PASS();
}

static int test_vec_persist(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 1 2 3]\n"
    "def v2 [vec-push $v 4]\n"
    "def v3 [vec-set $v 0 99]\n"
    "[print [vec-len $v]]\n"
    "[print [vec-get $v 0]]\n"
    "[print [vec-len $v2]]\n"
    "[print [vec-get $v3 0]]",
    &cap, "3\n1\n4\n99\n"));
  TEST_PASS();
}

static int test_vec_concat(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def a [vec 1 2]\n"
    "def b [vec 3 4]\n"
    "def c [vec-concat $a $b]\n"
    "[print [vec-len $c]]\n"
    "[print [vec-get $c 0]]\n"
    "[print [vec-get $c 3]]",
    &cap, "4\n1\n4\n"));
  TEST_PASS();
}

static int test_vec_slice(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 10 20 30 40 50]\n"
    "def s [vec-slice $v 1 3]\n"
    "[print [vec-len $s]]\n"
    "[print [vec-get $s 0]]\n"
    "[print [vec-get $s 1]]",
    &cap, "2\n20\n30\n"));
  TEST_PASS();
}

static int test_vec_arity_push(void) {
  ASSERT(run_err("[vec-push [vec]]", "builtin 'vec-push' expects 2 arguments"));
  TEST_PASS();
}

static int test_vec_arity_set(void) {
  ASSERT(run_err("[vec-set [vec] 0]", "builtin 'vec-set' expects 3 arguments"));
  TEST_PASS();
}

static int test_vec_arity_concat(void) {
  ASSERT(run_err("[vec-concat [vec]]", "builtin 'vec-concat' expects 2 arguments"));
  TEST_PASS();
}

static int test_vec_arity_slice(void) {
  ASSERT(run_err("[vec-slice [vec] 0]", "builtin 'vec-slice' expects 3 arguments"));
  TEST_PASS();
}

/* ===== US-004: Map constructor and basic accessors ===== */

static int test_map_construct_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [map-len [map]]]", &cap, "0\n"));
  TEST_PASS();
}

static int test_map_construct_pairs(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map \"a\" 1 \"b\" 2 \"c\" 3 \"d\" 4]\n"
    "[print [map-len $m]]",
    &cap, "4\n"));
  TEST_PASS();
}

static int test_map_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"x\" 42]\n"
    "[print [map-get $m \"x\"]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_map_get_missing(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 1]\n"
    "[print [map-get $m \"zzz\"]]",
    &cap, "nil\n"));
  TEST_PASS();
}

static int test_map_has(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 1]\n"
    "[print [map-has $m \"a\"]]\n"
    "[print [map-has $m \"b\"]]",
    &cap, "true\nfalse\n"));
  TEST_PASS();
}

static int test_map_len(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [map-len [map]]]\n"
    "[print [map-len [map \"a\" 1 \"b\" 2 \"c\" 3]]]",
    &cap, "0\n3\n"));
  TEST_PASS();
}

static int test_map_mixed_keys(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map 1 \"one\" $true \"yes\"]\n"
    "[print [map-get $m 1]]\n"
    "[print [map-get $m $true]]",
    &cap, "one\nyes\n"));
  TEST_PASS();
}

static int test_map_arity_odd(void) {
  ASSERT(run_err("[map \"a\" 1 \"b\"]", "even number"));
  TEST_PASS();
}

static int test_map_arity_get(void) {
  ASSERT(run_err("[map-get [map]]", "builtin 'map-get' expects 2 arguments"));
  TEST_PASS();
}

static int test_map_arity_has(void) {
  ASSERT(run_err("[map-has [map]]", "builtin 'map-has' expects 2 arguments"));
  TEST_PASS();
}

static int test_map_arity_len(void) {
  ASSERT(run_err("[map-len]", "builtin 'map-len' expects 1 argument"));
  TEST_PASS();
}

/* ===== US-005: Map update operations ===== */

static int test_map_set(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 1]\n"
    "def m2 [map-set $m \"b\" 2]\n"
    "[print [map-get $m2 \"a\"]]\n"
    "[print [map-get $m2 \"b\"]]\n"
    "[print [map-len $m2]]",
    &cap, "1\n2\n2\n"));
  TEST_PASS();
}

static int test_map_set_overwrite(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 1]\n"
    "def m2 [map-set $m \"a\" 99]\n"
    "[print [map-get $m2 \"a\"]]",
    &cap, "99\n"));
  TEST_PASS();
}

static int test_map_remove(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map \"a\" 1 \"b\" 2]\n"
    "def m2 [map-remove $m \"a\"]\n"
    "[print [map-has $m2 \"a\"]]\n"
    "[print [map-get $m2 \"b\"]]\n"
    "[print [map-len $m2]]",
    &cap, "false\n2\n1\n"));
  TEST_PASS();
}

static int test_map_remove_missing(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 1]\n"
    "def m2 [map-remove $m \"zzz\"]\n"
    "[print [map-len $m2]]\n"
    "[print [map-get $m2 \"a\"]]",
    &cap, "1\n1\n"));
  TEST_PASS();
}

static int test_map_persist(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 1]\n"
    "def m2 [map-set $m \"b\" 2]\n"
    "def m3 [map-remove $m \"a\"]\n"
    "[print [map-len $m]]\n"
    "[print [map-has $m \"a\"]]\n"
    "[print [map-len $m2]]\n"
    "[print [map-len $m3]]",
    &cap, "1\ntrue\n2\n0\n"));
  TEST_PASS();
}

static int test_map_keys(void) {
  PrintCapture cap;
  /* Single-entry map for deterministic order */
  ASSERT(run_ok(
    "def m [map-set [map] \"x\" 1]\n"
    "def ks [map-keys $m]\n"
    "[print [vec-len $ks]]\n"
    "[print [vec-get $ks 0]]",
    &cap, "1\nx\n"));
  TEST_PASS();
}

static int test_map_vals(void) {
  PrintCapture cap;
  /* Single-entry map for deterministic order */
  ASSERT(run_ok(
    "def m [map-set [map] \"x\" 42]\n"
    "def vs [map-vals $m]\n"
    "[print [vec-len $vs]]\n"
    "[print [vec-get $vs 0]]",
    &cap, "1\n42\n"));
  TEST_PASS();
}

static int test_map_arity_set(void) {
  ASSERT(run_err("[map-set [map] \"a\"]", "builtin 'map-set' expects 3 arguments"));
  TEST_PASS();
}

static int test_map_arity_remove(void) {
  ASSERT(run_err("[map-remove [map]]", "builtin 'map-remove' expects 2 arguments"));
  TEST_PASS();
}

static int test_map_arity_keys(void) {
  ASSERT(run_err("[map-keys]", "builtin 'map-keys' expects 1 argument"));
  TEST_PASS();
}

static int test_map_arity_vals(void) {
  ASSERT(run_err("[map-vals]", "builtin 'map-vals' expects 1 argument"));
  TEST_PASS();
}

/* ===== US-006: Structural equality ===== */

static int test_eq_vec_equal(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [== [vec 1 2 3] [vec 1 2 3]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_eq_vec_diff_len(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [== [vec 1 2] [vec 1 2 3]]]", &cap, "false\n"));
  TEST_PASS();
}

static int test_eq_map_equal(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def a [map-set [map] \"a\" 1]\n"
    "def b [map-set [map] \"a\" 1]\n"
    "[print [== $a $b]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_eq_map_order_irrelevant(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def a [map \"a\" 1 \"b\" 2]\n"
    "def b [map \"b\" 2 \"a\" 1]\n"
    "[print [== $a $b]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_eq_nested(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [== [vec [vec 1] [vec 2]] [vec [vec 1] [vec 2]]]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_eq_cross_type(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [== [vec 1] [map-set [map] \"a\" 1]]]\n"
    "[print [== [vec 1] 1]]",
    &cap, "false\nfalse\n"));
  TEST_PASS();
}

/* ===== US-007: Print and to-string ===== */

static int test_print_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [vec 1 2 3]]\n"
    "[print [vec]]",
    &cap, "[vec 1 2 3]\n[vec]\n"));
  TEST_PASS();
}

static int test_print_map(void) {
  PrintCapture cap;
  /* Single-entry to avoid HAMT order issues */
  ASSERT(run_ok(
    "[print [map-set [map] \"a\" 1]]\n"
    "[print [map]]",
    &cap, "[map \"a\" 1]\n[map]\n"));
  TEST_PASS();
}

static int test_print_nested(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [vec [vec 1] [map-set [map] \"x\" 2]]]",
    &cap, "[vec [vec 1] [map \"x\" 2]]\n"));
  TEST_PASS();
}

static int test_tostring_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [to-string [vec 1 2]]]",
    &cap, "[vec 1 2]\n"));
  TEST_PASS();
}

static int test_tostring_map(void) {
  PrintCapture cap;
  /* Single-entry for deterministic output */
  ASSERT(run_ok(
    "[print [to-string [map-set [map] \"a\" 1]]]",
    &cap, "[map \"a\" 1]\n"));
  TEST_PASS();
}

/* ===== US-008: each builtin ===== */

static int test_each_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 10 20 30]\n"
    "proc p {x} { print $x }\n"
    "[for $v $p]",
    &cap, "10\n20\n30\n"));
  TEST_PASS();
}

static int test_each_map(void) {
  PrintCapture cap;
  /* Single-entry for deterministic order */
  ASSERT(run_ok(
    "def m [map-set [map] \"k\" 42]\n"
    "proc p {k, v} { print $k\nprint $v }\n"
    "[for $m $p]",
    &cap, "k\n42\n"));
  TEST_PASS();
}

static int test_each_nil_return(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 1]\n"
    "proc p {x} { + $x 0 }\n"
    "[print [for $v $p]]",
    &cap, "nil\n"));
  TEST_PASS();
}

static int test_each_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc p {x} { print $x }\n"
    "[for [vec] $p]",
    &cap, ""));
  TEST_PASS();
}

static int test_each_arity(void) {
  ASSERT(run_err("[each [vec 1]]", "undefined variable '$each'"));
  TEST_PASS();
}

/* ===== US-009/US-012: transform builtin ===== */

static int test_transform_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 1 2 3]\n"
    "proc inc {x} { + $x 10 }\n"
    "def v2 [transform $v $inc]\n"
    "[print [vec-get $v2 0]]\n"
    "[print [vec-get $v2 1]]\n"
    "[print [vec-get $v2 2]]",
    &cap, "11\n12\n13\n"));
  TEST_PASS();
}

static int test_transform_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc inc {x} { + $x 1 }\n"
    "[print [vec-len [transform [vec] $inc]]]",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_transform_map(void) {
  PrintCapture cap;
  /* Single-entry for determinism */
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 5]\n"
    "proc dbl {k, v} { vec $k [* $v 2] }\n"
    "def m2 [transform $m $dbl]\n"
    "[print [map-get $m2 \"a\"]]",
    &cap, "10\n"));
  TEST_PASS();
}

/* ===== US-010: filter builtin ===== */

static int test_filter_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 1 2 3 4 5]\n"
    "proc gt2 {x} { > $x 2 }\n"
    "def v2 [filter $v $gt2]\n"
    "[print [vec-len $v2]]\n"
    "[print [vec-get $v2 0]]\n"
    "[print [vec-get $v2 1]]\n"
    "[print [vec-get $v2 2]]",
    &cap, "3\n3\n4\n5\n"));
  TEST_PASS();
}

static int test_filter_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc gt2 {x} { > $x 2 }\n"
    "[print [vec-len [filter [vec] $gt2]]]",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_filter_map(void) {
  PrintCapture cap;
  /* Use single-entry maps: one that passes, one that doesn't */
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 10]\n"
    "proc big {k, v} { > $v 5 }\n"
    "def m2 [filter $m $big]\n"
    "[print [map-len $m2]]\n"
    "[print [map-get $m2 \"a\"]]",
    &cap, "1\n10\n"));
  TEST_PASS();
}

static int test_filter_map_exclude(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 1]\n"
    "proc big {k, v} { > $v 5 }\n"
    "def m2 [filter $m $big]\n"
    "[print [map-len $m2]]",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_filter_arity(void) {
  ASSERT(run_err("[filter [vec 1]]", "builtin 'filter' expects 2 arguments"));
  TEST_PASS();
}

/* ===== Error cases: type errors ===== */

static int test_vec_get_type_err(void) {
  ASSERT(run_err("[vec-get 42 0]", "expected vector"));
  TEST_PASS();
}

static int test_map_get_type_err(void) {
  ASSERT(run_err("[map-get 42 \"a\"]", "expected map"));
  TEST_PASS();
}

static int test_each_type_err(void) {
  ASSERT(run_err(
    "proc p {x} { + $x 0 }\n"
    "[for 42 $p]",
    "expected vector, map, or stream"));
  TEST_PASS();
}

/* ===== Persistence: comprehensive check ===== */

static int test_persist_vec_comprehensive(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def v [vec 1 2 3]\n"
    "def a [vec-push $v 4]\n"
    "def b [vec-set $v 0 99]\n"
    "def c [vec-concat $v [vec 5 6]]\n"
    "def d [vec-slice $v 1 3]\n"
    /* Original unchanged */
    "[print [vec-len $v]]\n"
    "[print [vec-get $v 0]]\n"
    "[print [vec-get $v 1]]\n"
    "[print [vec-get $v 2]]\n"
    /* Push result */
    "[print [vec-len $a]]\n"
    /* Set result */
    "[print [vec-get $b 0]]\n"
    /* Concat result */
    "[print [vec-len $c]]\n"
    /* Slice result */
    "[print [vec-len $d]]",
    &cap, "3\n1\n2\n3\n4\n99\n5\n2\n"));
  TEST_PASS();
}

static int test_persist_map_comprehensive(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"a\" 1]\n"
    "def m2 [map-set $m \"b\" 2]\n"
    "def m3 [map-remove $m \"a\"]\n"
    /* Original unchanged */
    "[print [map-len $m]]\n"
    "[print [map-get $m \"a\"]]\n"
    /* set result has both */
    "[print [map-len $m2]]\n"
    "[print [map-get $m2 \"b\"]]\n"
    /* remove result is empty */
    "[print [map-len $m3]]",
    &cap, "1\n1\n2\n2\n0\n"));
  TEST_PASS();
}

/* ===== Combined / End-to-end integration ===== */

static int test_vec_in_map(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"nums\" [vec 10 20 30]]\n"
    "def v [map-get $m \"nums\"]\n"
    "[print [vec-get $v 1]]",
    &cap, "20\n"));
  TEST_PASS();
}

static int test_map_in_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map-set [map] \"x\" 99]\n"
    "def v [vec $m]\n"
    "def got [vec-get $v 0]\n"
    "[print [map-get $got \"x\"]]",
    &cap, "99\n"));
  TEST_PASS();
}

static int test_chained_transforms(void) {
  PrintCapture cap;
  /* Map then filter: double elements, keep > 5 */
  ASSERT(run_ok(
    "def v [vec 1 2 3 4 5]\n"
    "proc dbl {x} { * $x 2 }\n"
    "proc gt5 {x} { > $x 5 }\n"
    "def v2 [filter [transform $v $dbl] $gt5]\n"
    "[print [vec-len $v2]]\n"
    "[print [vec-get $v2 0]]\n"
    "[print [vec-get $v2 1]]\n"
    "[print [vec-get $v2 2]]",
    &cap, "3\n6\n8\n10\n"));
  TEST_PASS();
}

static int test_multiline_program(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def items [vec 10 20 30 40 50]\n"
    "proc gt25 {x} { > $x 25 }\n"
    "def big [filter $items $gt25]\n"
    "proc add1 {x} { + $x 1 }\n"
    "def res [transform $big $add1]\n"
    "[print [vec-get $res 0]]\n"
    "[print [vec-get $res 1]]\n"
    "[print [vec-get $res 2]]",
    &cap, "31\n41\n51\n"));
  TEST_PASS();
}

/* ===== US-017: M8a design fixes test coverage ===== */

static int test_map_construct_single_pair(void) {
  PrintCapture cap;
  /* [map "a" 1] creates a 1-pair map (was transform path pre-M8a) */
  ASSERT(run_ok(
    "def m [map \"a\" 1]\n"
    "[print [map-len $m]]\n"
    "[print [map-get $m \"a\"]]",
    &cap, "1\n1\n"));
  TEST_PASS();
}

static int test_transform_empty_map(void) {
  PrintCapture cap;
  /* Transforming an empty map returns an empty map */
  ASSERT(run_ok(
    "proc dbl {k, v} { vec $k [* $v 2] }\n"
    "[print [map-len [transform [map] $dbl]]]",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_transform_arity(void) {
  ASSERT(run_err("[transform [vec 1]]", "builtin 'transform' expects 2 arguments"));
  TEST_PASS();
}

/* ===== US-016: Collections as map keys ===== */

static int test_vec_as_map_key(void) {
  PrintCapture cap;
  /* Structurally equal vectors should find same entry */
  ASSERT(run_ok(
    "[print [map-get [map [vec 1] \"a\"] [vec 1]]]",
    &cap, "a\n"));
  TEST_PASS();
}

static int test_map_as_map_key(void) {
  PrintCapture cap;
  /* Structurally equal maps should find same entry */
  ASSERT(run_ok(
    "[print [map-get [map [map \"x\" 1] \"nested\"] [map \"x\" 1]]]",
    &cap, "nested\n"));
  TEST_PASS();
}

static int test_nested_coll_as_key(void) {
  PrintCapture cap;
  /* Nested collection as key */
  ASSERT(run_ok(
    "def k [vec [vec 1 2] [vec 3]]\n"
    "def m [map $k \"deep\"]\n"
    "[print [map-get $m [vec [vec 1 2] [vec 3]]]]",
    &cap, "deep\n"));
  TEST_PASS();
}

static int test_vec_key_not_found(void) {
  PrintCapture cap;
  /* Different vector should not match */
  ASSERT(run_ok(
    "def m [map [vec 1] \"a\"]\n"
    "[print [map-get $m [vec 2]]]",
    &cap, "nil\n"));
  TEST_PASS();
}

static int test_vec_key_has(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def m [map [vec 1 2] \"yes\"]\n"
    "[print [map-has $m [vec 1 2]]]\n"
    "[print [map-has $m [vec 1 3]]]",
    &cap, "true\nfalse\n"));
  TEST_PASS();
}

static int test_eq_still_works(void) {
  PrintCapture cap;
  /* Verify eq still uses structural equality (no regression) */
  ASSERT(run_ok(
    "[print [== [vec 1 2 3] [vec 1 2 3]]]\n"
    "[print [== [vec 1] [vec 2]]]\n"
    "[print [== [map \"a\" 1] [map \"a\" 1]]]\n"
    "[print [== 42 42]]\n"
    "[print [== \"hello\" \"hello\"]]",
    &cap, "true\nfalse\ntrue\ntrue\ntrue\n"));
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-002: Vector construction and basic accessors */
    { "vec_construct_empty",       test_vec_construct_empty },
    { "vec_construct_elements",    test_vec_construct_elements },
    { "vec_get",                   test_vec_get },
    { "vec_get_oob",               test_vec_get_oob },
    { "vec_len",                   test_vec_len },
    { "vec_mixed_types",           test_vec_mixed_types },
    { "vec_arity_get",             test_vec_arity_get },
    { "vec_arity_len",             test_vec_arity_len },
    /* US-003: Vector update operations */
    { "vec_push",                  test_vec_push },
    { "vec_set",                   test_vec_set },
    { "vec_set_oob",               test_vec_set_oob },
    { "vec_set_empty",             test_vec_set_empty },
    { "vec_set_empty_gap",         test_vec_set_empty_gap },
    { "vec_set_neg",               test_vec_set_neg },
    { "vec_set_oob_persist",       test_vec_set_oob_persist },
    { "vec_persist",               test_vec_persist },
    { "vec_concat",                test_vec_concat },
    { "vec_slice",                 test_vec_slice },
    { "vec_arity_push",            test_vec_arity_push },
    { "vec_arity_set",             test_vec_arity_set },
    { "vec_arity_concat",          test_vec_arity_concat },
    { "vec_arity_slice",           test_vec_arity_slice },
    /* US-004: Map constructor and basic accessors */
    { "map_construct_empty",       test_map_construct_empty },
    { "map_construct_pairs",       test_map_construct_pairs },
    { "map_get",                   test_map_get },
    { "map_get_missing",           test_map_get_missing },
    { "map_has",                   test_map_has },
    { "map_len",                   test_map_len },
    { "map_mixed_keys",            test_map_mixed_keys },
    { "map_arity_odd",             test_map_arity_odd },
    { "map_arity_get",             test_map_arity_get },
    { "map_arity_has",             test_map_arity_has },
    { "map_arity_len",             test_map_arity_len },
    /* US-005: Map update operations */
    { "map_set",                   test_map_set },
    { "map_set_overwrite",         test_map_set_overwrite },
    { "map_remove",                test_map_remove },
    { "map_remove_missing",        test_map_remove_missing },
    { "map_persist",               test_map_persist },
    { "map_keys",                  test_map_keys },
    { "map_vals",                  test_map_vals },
    { "map_arity_set",             test_map_arity_set },
    { "map_arity_remove",          test_map_arity_remove },
    { "map_arity_keys",            test_map_arity_keys },
    { "map_arity_vals",            test_map_arity_vals },
    /* US-006: Structural equality */
    { "eq_vec_equal",              test_eq_vec_equal },
    { "eq_vec_diff_len",           test_eq_vec_diff_len },
    { "eq_map_equal",              test_eq_map_equal },
    { "eq_map_order_irrelevant",   test_eq_map_order_irrelevant },
    { "eq_nested",                 test_eq_nested },
    { "eq_cross_type",             test_eq_cross_type },
    /* US-007: Print and to-string */
    { "print_vec",                 test_print_vec },
    { "print_map",                 test_print_map },
    { "print_nested",              test_print_nested },
    { "tostring_vec",              test_tostring_vec },
    { "tostring_map",              test_tostring_map },
    /* US-008: each */
    { "each_vec",                  test_each_vec },
    { "each_map",                  test_each_map },
    { "each_nil_return",           test_each_nil_return },
    { "each_empty",                test_each_empty },
    { "each_arity",                test_each_arity },
    /* US-009/US-012: transform */
    { "transform_vec",             test_transform_vec },
    { "transform_empty",           test_transform_empty },
    { "transform_map",             test_transform_map },
    /* US-010: filter */
    { "filter_vec",                test_filter_vec },
    { "filter_empty",              test_filter_empty },
    { "filter_map",                test_filter_map },
    { "filter_map_exclude",        test_filter_map_exclude },
    { "filter_arity",              test_filter_arity },
    /* Error cases */
    { "vec_get_type_err",          test_vec_get_type_err },
    { "map_get_type_err",          test_map_get_type_err },
    { "each_type_err",             test_each_type_err },
    /* Persistence comprehensive */
    { "persist_vec_comprehensive", test_persist_vec_comprehensive },
    { "persist_map_comprehensive", test_persist_map_comprehensive },
    /* US-017: M8a design fixes */
    { "map_construct_single_pair",  test_map_construct_single_pair },
    { "transform_empty_map",        test_transform_empty_map },
    { "transform_arity",            test_transform_arity },
    /* US-016: Collections as map keys */
    { "vec_as_map_key",            test_vec_as_map_key },
    { "map_as_map_key",            test_map_as_map_key },
    { "nested_coll_as_key",        test_nested_coll_as_key },
    { "vec_key_not_found",         test_vec_key_not_found },
    { "vec_key_has",               test_vec_key_has },
    { "eq_still_works",            test_eq_still_works },
    /* End-to-end integration */
    { "vec_in_map",                test_vec_in_map },
    { "map_in_vec",                test_map_in_vec },
    { "chained_transforms",        test_chained_transforms },
    { "multiline_program",         test_multiline_program },
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
