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

/* ===== Typed Map: Construction ===== */

static int test_typed_map_construct_empty(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [map-len [[Map Point]]]]",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_typed_map_construct_elements(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "[print [map-len [[Map Point] \"a\" [Point 1 2] \"b\" [Point 3 4]]]]",
    &cap, "2\n"));
  TEST_PASS();
}

/* ===== Typed Map: Get ===== */

static int test_typed_map_get(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Point] \"a\" [Point 10 20] \"b\" [Point 30 40]]\n"
    "def p [map-get $m \"a\"]\n"
    "[print $p->x]\n"
    "[print $p->y]\n"
    "def q [map-get $m \"b\"]\n"
    "[print $q->x]\n"
    "[print $q->y]",
    &cap, "10\n20\n30\n40\n"));
  TEST_PASS();
}

static int test_typed_map_get_missing(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Point] \"a\" [Point 1 2]]\n"
    "[print [map-get $m \"zzz\"]]",
    &cap, "nil\n"));
  TEST_PASS();
}

/* ===== Typed Map: Has ===== */

static int test_typed_map_has(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Point] \"x\" [Point 1 2]]\n"
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
    "def m [[Map Point] \"a\" [Point 1 2]]\n"
    "def m2 [map-set $m \"b\" [Point 3 4]]\n"
    "[print [map-len $m2]]\n"
    "def p [map-get $m2 \"b\"]\n"
    "[print $p->x]\n"
    "[print $p->y]\n"
    "# original unchanged (persistent)\n"
    "[print [map-len $m]]",
    &cap, "2\n3\n4\n1\n"));
  TEST_PASS();
}

static int test_typed_map_set_overwrite(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Point] \"a\" [Point 1 2]]\n"
    "def m2 [map-set $m \"a\" [Point 99 88]]\n"
    "[print [map-len $m2]]\n"
    "def p [map-get $m2 \"a\"]\n"
    "[print $p->x]\n"
    "[print $p->y]",
    &cap, "1\n99\n88\n"));
  TEST_PASS();
}

/* ===== Typed Map: Remove ===== */

static int test_typed_map_remove(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Point] \"a\" [Point 1 2] \"b\" [Point 3 4]]\n"
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
    "[print [map-len [[Map Point]]]]\n"
    "[print [map-len [[Map Point] \"a\" [Point 1 2]]]]\n"
    "[print [map-len [[Map Point] \"a\" [Point 1 2] \"b\" [Point 3 4] \"c\" [Point 5 6]]]]",
    &cap, "0\n1\n3\n"));
  TEST_PASS();
}

/* ===== Typed Map: Keys ===== */

static int test_typed_map_keys(void) {
  PrintCapture cap;
  /* keys returns a dyn vec — test length */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Point] \"x\" [Point 1 2] \"y\" [Point 3 4]]\n"
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
    "def m [[Map Point] \"a\" [Point 10 20]]\n"
    "def vals [map-vals $m]\n"
    "[print [vec-len $vals]]\n"
    "def p [vec-get $vals 0]\n"
    "[print $p->x]\n"
    "[print $p->y]",
    &cap, "1\n10\n20\n"));
  TEST_PASS();
}

/* ===== Typed Map: Wide struct (4 fields) ===== */

static int test_typed_map_wide_struct(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Rect {i32 x, i32 y, i32 w, i32 h}\n"
    "def m [[Map Rect] \"a\" [Rect 10 20 100 200] \"b\" [Rect 30 40 300 400]]\n"
    "[print [map-len $m]]\n"
    "def r [map-get $m \"a\"]\n"
    "[print $r->x]\n"
    "[print $r->w]\n"
    "def s [map-get $m \"b\"]\n"
    "[print $s->y]\n"
    "[print $s->h]",
    &cap, "2\n10\n100\n40\n400\n"));
  TEST_PASS();
}

/* ===== Typed Map: Persistence ===== */

static int test_typed_map_persistence(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def a [[Map Point] \"k\" [Point 1 2]]\n"
    "def b [map-set $a \"k2\" [Point 3 4]]\n"
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
    "def m [[Map Point] 42 [Point 1 2] 99 [Point 3 4]]\n"
    "def p [map-get $m 42]\n"
    "[print $p->x]\n"
    "[print $p->y]\n"
    "[print [map-has $m 99]]\n"
    "[print [map-has $m 0]]",
    &cap, "1\n2\ntrue\nfalse\n"));
  TEST_PASS();
}

/* ===== Typed Map: GC safety ===== */

static int test_typed_map_gc_safety(void) {
  PrintCapture cap;
  /* Chain set operations to allocate many nodes, exercising GC */
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "def m [[Map Point]]\n"
    "def m [map-set $m \"a\" [Point 1 2]]\n"
    "def m [map-set $m \"b\" [Point 3 4]]\n"
    "def m [map-set $m \"c\" [Point 5 6]]\n"
    "def m [map-set $m \"d\" [Point 7 8]]\n"
    "def m [map-set $m \"e\" [Point 9 10]]\n"
    "[print [map-len $m]]\n"
    "def p [map-get $m \"a\"]\n"
    "[print $p->x]\n"
    "def q [map-get $m \"e\"]\n"
    "[print $q->y]",
    &cap, "5\n1\n10\n"));
  TEST_PASS();
}

/* ===== Typed Map: Closure capture (upvalue path) ===== */

static int test_typed_map_closure_capture(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct Point {i32 x, i32 y}\n"
    "proc make-lookup {} {\n"
    "  def m [[Map Point] \"origin\" [Point 0 0]]\n"
    "  proc count {} { [map-len $m] }\n"
    "  $count\n"
    "}\n"
    "def counter [make-lookup]\n"
    "[print [counter]]",
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

  printf("=== Typed Map Tests ===\n");

  RUN(test_typed_map_construct_empty);
  RUN(test_typed_map_construct_elements);
  RUN(test_typed_map_get);
  RUN(test_typed_map_get_missing);
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

  printf("\n%d/%d passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
