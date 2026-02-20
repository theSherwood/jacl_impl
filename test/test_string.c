#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-001: Heap string type and intern table ===== */

/* Test: intern a string and get back a JACL_TAG_STRING value */
static int test_intern_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v = jacl_intern(&arena, &table, "hello world", 11);

  ASSERT(jacl_is_heap_string(v));
  ASSERT(jacl_is_string(v));

  JaclHeapString* hs = jacl_as_heap_string(v);
  ASSERT_U32_EQ(hs->length, 11);
  ASSERT(memcmp(hs->data, "hello world", 11) == 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: interning same bytes twice returns same pointer (deduplication) */
static int test_intern_dedup(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v1 = jacl_intern(&arena, &table, "hello world", 11);
  JaclVal v2 = jacl_intern(&arena, &table, "hello world", 11);

  /* Pointer equality guaranteed by interning */
  ASSERT_U64_EQ(v1, v2);

  JaclHeapString* hs1 = jacl_as_heap_string(v1);
  JaclHeapString* hs2 = jacl_as_heap_string(v2);
  ASSERT_PTR_EQ(hs1, hs2);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: different strings get different pointers */
static int test_intern_different(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v1 = jacl_intern(&arena, &table, "hello world", 11);
  JaclVal v2 = jacl_intern(&arena, &table, "goodbye world", 13);

  ASSERT(v1 != v2);

  JaclHeapString* hs1 = jacl_as_heap_string(v1);
  JaclHeapString* hs2 = jacl_as_heap_string(v2);
  ASSERT(hs1 != hs2);
  ASSERT_U32_EQ(hs1->length, 11);
  ASSERT_U32_EQ(hs2->length, 13);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: strings with embedded NUL bytes intern correctly */
static int test_intern_embedded_nul(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  const char data_with_nul[] = "hel\0lo w\0rld";
  uint32_t len = 12;  /* includes the NUL bytes */

  JaclVal v1 = jacl_intern(&arena, &table, data_with_nul, len);
  JaclVal v2 = jacl_intern(&arena, &table, data_with_nul, len);

  /* Should still deduplicate correctly */
  ASSERT_U64_EQ(v1, v2);

  JaclHeapString* hs = jacl_as_heap_string(v1);
  ASSERT_U32_EQ(hs->length, 12);
  ASSERT(memcmp(hs->data, data_with_nul, 12) == 0);

  /* Different string without NUL should be different */
  JaclVal v3 = jacl_intern(&arena, &table, "hello world!", 12);
  ASSERT(v1 != v3);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash function produces consistent results */
static int test_fnv1a_hash(void) {
  uint32_t h1 = string__fnv1a("hello", 5);
  uint32_t h2 = string__fnv1a("hello", 5);
  ASSERT_U32_EQ(h1, h2);

  uint32_t h3 = string__fnv1a("world", 5);
  ASSERT(h1 != h3);  /* different strings should (almost certainly) differ */

  /* Hash uses length, not null-terminator */
  uint32_t h4 = string__fnv1a("hell", 4);
  ASSERT(h1 != h4);

  TEST_PASS();
}

/* Test: table resize triggers at > 0.75 load factor */
static int test_intern_resize(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  /* Initial cap is 16, so inserting 13+ should trigger resize */
  char buf[32];
  JaclVal values[20];
  for (int i = 0; i < 20; i++) {
    int n = snprintf(buf, sizeof(buf), "string number %02d", i);
    values[i] = jacl_intern(&arena, &table, buf, (uint32_t)n);
    ASSERT(jacl_is_heap_string(values[i]));
  }

  ASSERT_U32_EQ(table.count, 20);
  ASSERT(table.cap > INTERN_INIT_CAP);  /* must have resized */

  /* Verify all strings are still findable after resize */
  for (int i = 0; i < 20; i++) {
    int n = snprintf(buf, sizeof(buf), "string number %02d", i);
    JaclVal v = jacl_intern(&arena, &table, buf, (uint32_t)n);
    ASSERT_U64_EQ(v, values[i]);  /* pointer equality */
  }

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: is_heap_string predicate */
static int test_is_heap_string_predicate(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal heap_str = jacl_intern(&arena, &table, "long string here", 16);
  JaclVal inline_str = jacl_inline_string("short", 5);
  JaclVal integer = jacl_i32(42);
  JaclVal nil = JACL_NIL;

  ASSERT(jacl_is_heap_string(heap_str) == true);
  ASSERT(jacl_is_heap_string(inline_str) == false);
  ASSERT(jacl_is_heap_string(integer) == false);
  ASSERT(jacl_is_heap_string(nil) == false);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-002: Unified string access API ===== */

/* Test: jacl_string_len works for both inline and heap */
static int test_string_len(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal short_s = jacl_inline_string("hello", 5);
  JaclVal empty_s = jacl_inline_string("", 0);
  JaclVal long_s = jacl_intern(&arena, &table, "hello world!", 12);

  ASSERT_U32_EQ(jacl_string_len(short_s), 5);
  ASSERT_U32_EQ(jacl_string_len(empty_s), 0);
  ASSERT_U32_EQ(jacl_string_len(long_s), 12);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_data copies bytes for both types */
static int test_string_data(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal short_s = jacl_inline_string("abc", 3);
  JaclVal long_s = jacl_intern(&arena, &table, "hello world!", 12);

  char buf[32];

  /* Inline string */
  uint32_t len = jacl_string_data(short_s, buf, sizeof(buf));
  ASSERT_U32_EQ(len, 3);
  ASSERT(memcmp(buf, "abc", 3) == 0);

  /* Heap string */
  len = jacl_string_data(long_s, buf, sizeof(buf));
  ASSERT_U32_EQ(len, 12);
  ASSERT(memcmp(buf, "hello world!", 12) == 0);

  /* Truncated copy (buffer smaller than string) */
  len = jacl_string_data(long_s, buf, 5);
  ASSERT_U32_EQ(len, 12);  /* returns actual length */
  ASSERT(memcmp(buf, "hello", 5) == 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_is_string returns true for both types */
static int test_is_string_unified(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal short_s = jacl_inline_string("hi", 2);
  JaclVal long_s = jacl_intern(&arena, &table, "hello world!", 12);
  JaclVal integer = jacl_i32(42);
  JaclVal nil = JACL_NIL;

  ASSERT(jacl_is_string(short_s) == true);
  ASSERT(jacl_is_string(long_s) == true);
  ASSERT(jacl_is_string(integer) == false);
  ASSERT(jacl_is_string(nil) == false);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_eq — short==short */
static int test_string_eq_short_short(void) {
  JaclVal a = jacl_inline_string("abc", 3);
  JaclVal b = jacl_inline_string("abc", 3);
  JaclVal c = jacl_inline_string("def", 3);

  ASSERT(jacl_string_eq(a, b) == true);
  ASSERT(jacl_string_eq(a, c) == false);
  TEST_PASS();
}

/* Test: jacl_string_eq — long==long (heap pointer equality) */
static int test_string_eq_long_long(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal a = jacl_intern(&arena, &table, "hello world!", 12);
  JaclVal b = jacl_intern(&arena, &table, "hello world!", 12);
  JaclVal c = jacl_intern(&arena, &table, "goodbye world", 13);

  ASSERT(jacl_string_eq(a, b) == true);
  ASSERT(jacl_string_eq(a, c) == false);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_eq — short!=long */
static int test_string_eq_short_long(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal short_s = jacl_inline_string("hello", 5);
  JaclVal long_s = jacl_intern(&arena, &table, "hello world!", 12);

  ASSERT(jacl_string_eq(short_s, long_s) == false);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_eq — cross-representation equality edge case */
static int test_string_eq_cross_rep(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  /* Intern a short string (<=7 bytes) as heap string */
  JaclVal heap_hi = jacl_intern(&arena, &table, "hi", 2);
  JaclVal inline_hi = jacl_inline_string("hi", 2);

  /* Different representations, same content */
  ASSERT(jacl_is_heap_string(heap_hi));
  ASSERT(jacl_is_inline_string(inline_hi));
  ASSERT(jacl_string_eq(heap_hi, inline_hi) == true);
  ASSERT(jacl_string_eq(inline_hi, heap_hi) == true);

  /* Different content */
  JaclVal inline_no = jacl_inline_string("no", 2);
  ASSERT(jacl_string_eq(heap_hi, inline_no) == false);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_cmp — lexicographic ordering */
static int test_string_cmp_ordering(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal abc = jacl_inline_string("abc", 3);
  JaclVal def = jacl_inline_string("def", 3);
  JaclVal ab = jacl_inline_string("ab", 2);
  JaclVal abc2 = jacl_inline_string("abc", 3);
  JaclVal long_abc = jacl_intern(&arena, &table, "abcdefghij", 10);
  JaclVal long_xyz = jacl_intern(&arena, &table, "xyzxyzxyzx", 10);

  /* short < short */
  ASSERT(jacl_string_cmp(abc, def) < 0);
  ASSERT(jacl_string_cmp(def, abc) > 0);

  /* Equal strings */
  ASSERT(jacl_string_cmp(abc, abc2) == 0);

  /* Prefix ordering: "ab" < "abc" */
  ASSERT(jacl_string_cmp(ab, abc) < 0);
  ASSERT(jacl_string_cmp(abc, ab) > 0);

  /* long < long */
  ASSERT(jacl_string_cmp(long_abc, long_xyz) < 0);
  ASSERT(jacl_string_cmp(long_xyz, long_abc) > 0);

  /* short < long (cross-rep) */
  ASSERT(jacl_string_cmp(abc, long_xyz) < 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-003: Compiler support for heap string literals ===== */

/* Helper: compile source with an intern table */
static CompileResult compile_with_intern(const char* source, arena_t* arena,
                                          JaclInternTable* table) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  return compiler_compile(parse, arena, table);
}

/* Test: short string literal (<=7 bytes) still produces inline constant */
static int test_compile_short_string_still_inline(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  CompileResult cr = compile_with_intern("\"hello\"", &arena, &table);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Should have one constant: an inline string */
  ASSERT(cr.chunk.const_count >= 1);
  JaclVal val = cr.chunk.constants[0];
  ASSERT(jacl_is_inline_string(val));
  ASSERT_U32_EQ(jacl_string_len(val), 5);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: long string literal (>7 bytes) compiles without error, produces heap string */
static int test_compile_long_string_interns(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  CompileResult cr = compile_with_intern("\"hello world\"", &arena, &table);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Should have one constant: a heap string */
  ASSERT(cr.chunk.const_count >= 1);
  JaclVal val = cr.chunk.constants[0];
  ASSERT(jacl_is_heap_string(val));

  JaclHeapString* hs = jacl_as_heap_string(val);
  ASSERT_U32_EQ(hs->length, 11);
  ASSERT(memcmp(hs->data, "hello world", 11) == 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: duplicate long string literals share the same interned pointer */
static int test_compile_dup_long_strings_share_pointer(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  /* Two statements using the same long literal */
  CompileResult cr = compile_with_intern(
      "[print \"hello world\"] [print \"hello world\"]", &arena, &table);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Find the heap string constants — both should be the same JaclVal */
  JaclVal first_heap = 0;
  uint32_t heap_count = 0;
  for (uint32_t i = 0; i < cr.chunk.const_count; i++) {
    if (jacl_is_heap_string(cr.chunk.constants[i])) {
      if (heap_count == 0) {
        first_heap = cr.chunk.constants[i];
      } else {
        /* Same interned pointer: identical JaclVal (tag + pointer) */
        ASSERT_U64_EQ(cr.chunk.constants[i], first_heap);
      }
      heap_count++;
    }
  }
  ASSERT(heap_count >= 2);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [print "hello world"] compiles without error (previously rejected) */
static int test_compile_print_long_string_no_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  CompileResult cr = compile_with_intern("[print \"hello world\"]", &arena, &table);
  ASSERT_U32_EQ(cr.error_count, 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [def greeting "hello world"] compiles without error */
static int test_compile_def_long_string_no_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table;
  intern_table_init(&table, &arena);

  CompileResult cr = compile_with_intern(
      "[def greet \"hello world\"]", &arena, &table);
  ASSERT_U32_EQ(cr.error_count, 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-004: Print support for heap strings ===== */

/* Print capture helper */
typedef struct {
  char     buf[1024];
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

/* Test: [print "hello world"] prints the full heap string */
static int test_print_heap_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print \"hello world\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [print "short"] still works (inline strings unchanged) */
static int test_print_inline_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print \"short\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "short\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [print "hello world this is a long string"] prints without truncation */
static int test_print_long_heap_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print \"hello world this is a long string\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world this is a long string\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [def greet "hello world"] [print $greet] works end-to-end */
static int test_print_heap_string_via_variable(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[def greet \"hello world\"]\n[print $greet]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: printing both short and long strings in sequence */
static int test_print_mixed_strings(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print \"hi\"]\n[print \"hello world\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hi\nhello world\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-001: Heap string type and intern table */
    { "intern_basic",              test_intern_basic },
    { "intern_dedup",              test_intern_dedup },
    { "intern_different",          test_intern_different },
    { "intern_embedded_nul",       test_intern_embedded_nul },
    { "fnv1a_hash",                test_fnv1a_hash },
    { "intern_resize",             test_intern_resize },
    { "is_heap_string_predicate",  test_is_heap_string_predicate },
    /* US-002: Unified string access API */
    { "string_len",                test_string_len },
    { "string_data",               test_string_data },
    { "is_string_unified",         test_is_string_unified },
    { "string_eq_short_short",     test_string_eq_short_short },
    { "string_eq_long_long",       test_string_eq_long_long },
    { "string_eq_short_long",      test_string_eq_short_long },
    { "string_eq_cross_rep",       test_string_eq_cross_rep },
    { "string_cmp_ordering",       test_string_cmp_ordering },
    /* US-003: Compiler support for heap string literals */
    { "compile_short_string_still_inline",     test_compile_short_string_still_inline },
    { "compile_long_string_interns",           test_compile_long_string_interns },
    { "compile_dup_long_strings_share_pointer", test_compile_dup_long_strings_share_pointer },
    { "compile_print_long_string_no_error",    test_compile_print_long_string_no_error },
    { "compile_def_long_string_no_error",      test_compile_def_long_string_no_error },
    /* US-004: Print support for heap strings */
    { "print_heap_string",                     test_print_heap_string },
    { "print_inline_string",                   test_print_inline_string },
    { "print_long_heap_string",                test_print_long_heap_string },
    { "print_heap_string_via_variable",        test_print_heap_string_via_variable },
    { "print_mixed_strings",                   test_print_mixed_strings },
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
