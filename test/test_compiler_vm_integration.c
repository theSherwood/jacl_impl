#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-012: Compiler and VM integration for three-tier strings ===== */

/* Print capture helper */
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

/* Helper to set up VM with print capture */
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

/* --- Test: string interpolation basic --- */
static int test_interpolation_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "name = \"world\"\n"
    "[print \"hello $name\"]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: string interpolation with expression --- */
static int test_interpolation_expr(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print \"sum: $[+ 10 20]\"]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "sum: 30\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: string interpolation spanning tiers (concat grows to flat) --- */
static int test_interpolation_tier_span(void) {
  PrintCapture cap;
  /* "number=" + "42" + " end" = "number=42 end" (13 bytes, flat tier).
     Interpolation builds via OP_TO_STRING + OP_CONCAT. */
  VMResult r = run_capture(
    "n = 42\n"
    "[print \"number=$n end\"]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "number=42 end\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: empty interpolated string --- */
static int test_interpolation_empty(void) {
  PrintCapture cap;
  /* Test that empty string works through jacl_string_new */
  VMResult r = run_capture(
    "s = \"\"\n"
    "[print [length $s]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: OP_TO_STRING produces valid string (NFD is no-op for ASCII) --- */
static int test_to_string_via_interpolation(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "x = true\n"
    "y = 123\n"
    "[print \"$x $y\"]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true 123\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: print heap string (>7 bytes, ≤128 bytes) --- */
static int test_print_heap_string(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print \"hello world!\"]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world!\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: print rope string via C-level (>256 bytes, exercises cursor streaming) --- */
static int test_print_rope_string_c_level(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Build a 500-byte rope string, push to stack, print it */
  char input[500];
  for (int i = 0; i < 500; i++) input[i] = 'a' + (char)(i % 26);

  JaclVal rope_val = jacl_string_new(&vm.heap, vm.intern_table, input, 500);
  ASSERT(jacl_is_rope_string(rope_val));

  /* Exercise the print path by running a program that prints a known string,
     then we also test printing at C level via vm->print_fn directly. */
  /* For C-level print test, simulate what OP_PRINT does for large ropes */
  {
    uint32_t slen = jacl_string_byte_len(rope_val);
    ASSERT_U32_EQ(slen, 500);

    /* Use the rope cursor streaming path */
    JaclRopeString* rs = jacl_as_rope_string(rope_val);
    rope_cursor cur = rope_cursor_new(rs->r, 0);
    char buf[256];
    size_t remaining = slen;
    while (remaining > 0) {
      size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
      size_t got = rope_cursor_read(&cur, (uint8_t*)buf, chunk);
      if (got == 0) break;
      vm.print_fn(buf, (uint32_t)got, vm.print_ctx);
      rope_cursor_advance_bytes(&cur, got);
      remaining -= got;
    }
    rope_cursor_free(cur);
  }

  /* Verify we got exactly 500 bytes of correct content */
  ASSERT_U32_EQ(cap.len, 500);
  ASSERT(memcmp(cap.buf, input, 500) == 0);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: build large string via concat in JACL (rope tier) and print --- */
static int test_print_large_concat_string(void) {
  PrintCapture cap;
  /* Build a string > 128 bytes via repeated concat → rope.
     "abcdefghij" is 10 bytes. 20 repeats = 200 bytes → rope.
     Use a recursive proc. */
  VMResult r = run_capture(
    "proc rep {s, n} {\n"
    "  [if [== $n 0] { \"\" } { [concat $s [rep $s [- $n 1]]] }]\n"
    "}\n"
    "big = [rep \"abcdefghij\" 20]\n"
    "[print [byte-length $big]]\n"
    "[print [length $big]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "200\n200\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: string as proc argument and return --- */
static int test_string_proc_arg_return(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc id {s} { $s }\n"
    "[print [id \"hello\"]]\n"
    "[print [id \"hello world\"]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\nhello world\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: closure captures a string --- */
static int test_closure_captures_string(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc mk {s} {\n"
    "  proc get {} { $s }\n"
    "}\n"
    "f1 = [mk \"hello\"]\n"
    "f2 = [mk \"hello world\"]\n"
    "[print [f1]]\n"
    "[print [f2]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\nhello world\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: closure captures rope string (large) --- */
static int test_closure_captures_rope(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc rep {s, n} {\n"
    "  [if [== $n 0] { \"\" } { [concat $s [rep $s [- $n 1]]] }]\n"
    "}\n"
    "big = [rep \"abcdefghij\" 20]\n"
    "proc mkget {val} {\n"
    "  proc get {} { $val }\n"
    "}\n"
    "g = [mkget $big]\n"
    "[print [byte-length [g]]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "200\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: mixed tiers in single expression --- */
static int test_mixed_tiers_expression(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "s = \"hi\"\n"
    "m = \"abcdefghijk\"\n"
    "[print [concat $s \" \" $m]]\n"
    "[print [== $s \"hi\"]]\n"
    "[print [== $m \"abcdefghijk\"]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hi abcdefghijk\ntrue\ntrue\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: string literal compilation uses jacl_string_new (NFD path) --- */
static int test_literal_nfd_normalization(void) {
  /* NFC string literal "caf\xC3\xA9" should be NFD-normalized at compile time.
     Test at C level since we can't embed raw bytes in JACL source easily. */
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Simulate what the compiler does: jacl_string_new on a literal */
  JaclVal v = jacl_string_new(&heap, &table, "caf\xC3\xA9", 5);
  /* NFD: "cafe\xCC\x81" = 6 bytes, 4 graphemes */
  ASSERT_U32_EQ(jacl_string_byte_len(v), 6);
  ASSERT_U32_EQ(jacl_string_len(v), 4);

  /* Verify content */
  char buf[8];
  jacl_string_data(v, buf, sizeof(buf));
  ASSERT(buf[3] == 'e');
  ASSERT((uint8_t)buf[4] == 0xCC);
  ASSERT((uint8_t)buf[5] == 0x81);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "interpolation_basic",            test_interpolation_basic },
    { "interpolation_expr",             test_interpolation_expr },
    { "interpolation_tier_span",        test_interpolation_tier_span },
    { "interpolation_empty",            test_interpolation_empty },
    { "to_string_via_interpolation",    test_to_string_via_interpolation },
    { "print_heap_string",              test_print_heap_string },
    { "print_rope_string_c_level",      test_print_rope_string_c_level },
    { "print_large_concat_string",      test_print_large_concat_string },
    { "string_proc_arg_return",         test_string_proc_arg_return },
    { "closure_captures_string",        test_closure_captures_string },
    { "closure_captures_rope",          test_closure_captures_rope },
    { "mixed_tiers_expression",         test_mixed_tiers_expression },
    { "literal_nfd_normalization",      test_literal_nfd_normalization },
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
