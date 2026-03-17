#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-001: Heap string type and intern table ===== */

/* Test: intern a string and get back a JACL_TAG_STRING value */
static int test_intern_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v = jacl_intern(&heap, &table, "hello world", 11);

  ASSERT(jacl_is_heap_string(v));
  ASSERT(jacl_is_string(v));

  JaclHeapString* hs = jacl_as_heap_string(v);
  ASSERT_U32_EQ(hs->byte_len, 11);
  ASSERT(memcmp(hs->data, "hello world", 11) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: interning same bytes twice returns same pointer (deduplication) */
static int test_intern_dedup(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v1 = jacl_intern(&heap, &table, "hello world", 11);
  JaclVal v2 = jacl_intern(&heap, &table, "hello world", 11);

  /* Pointer equality guaranteed by interning */
  ASSERT_U64_EQ(v1, v2);

  JaclHeapString* hs1 = jacl_as_heap_string(v1);
  JaclHeapString* hs2 = jacl_as_heap_string(v2);
  ASSERT_PTR_EQ(hs1, hs2);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: different strings get different pointers */
static int test_intern_different(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v1 = jacl_intern(&heap, &table, "hello world", 11);
  JaclVal v2 = jacl_intern(&heap, &table, "goodbye world", 13);

  ASSERT(v1 != v2);

  JaclHeapString* hs1 = jacl_as_heap_string(v1);
  JaclHeapString* hs2 = jacl_as_heap_string(v2);
  ASSERT(hs1 != hs2);
  ASSERT_U32_EQ(hs1->byte_len, 11);
  ASSERT_U32_EQ(hs2->byte_len, 13);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: strings with embedded NUL bytes intern correctly */
static int test_intern_embedded_nul(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclInternTable table;
  intern_table_init(&table, &arena);

  const char data_with_nul[] = "hel\0lo w\0rld";
  uint32_t len = 12;  /* includes the NUL bytes */

  JaclVal v1 = jacl_intern(&heap, &table, data_with_nul, len);
  JaclVal v2 = jacl_intern(&heap, &table, data_with_nul, len);

  /* Should still deduplicate correctly */
  ASSERT_U64_EQ(v1, v2);

  JaclHeapString* hs = jacl_as_heap_string(v1);
  ASSERT_U32_EQ(hs->byte_len, 12);
  ASSERT(memcmp(hs->data, data_with_nul, 12) == 0);

  /* Different string without NUL should be different */
  JaclVal v3 = jacl_intern(&heap, &table, "hello world!", 12);
  ASSERT(v1 != v3);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
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
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclInternTable table;
  intern_table_init(&table, &arena);

  /* Initial cap is 16, so inserting 13+ should trigger resize */
  char buf[32];
  JaclVal values[20];
  for (int i = 0; i < 20; i++) {
    int n = snprintf(buf, sizeof(buf), "string number %02d", i);
    values[i] = jacl_intern(&heap, &table, buf, (uint32_t)n);
    ASSERT(jacl_is_heap_string(values[i]));
  }

  ASSERT_U32_EQ(table.count, 20);
  ASSERT(table.cap > INTERN_INIT_CAP);  /* must have resized */

  /* Verify all strings are still findable after resize */
  for (int i = 0; i < 20; i++) {
    int n = snprintf(buf, sizeof(buf), "string number %02d", i);
    JaclVal v = jacl_intern(&heap, &table, buf, (uint32_t)n);
    ASSERT_U64_EQ(v, values[i]);  /* pointer equality */
  }

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: is_heap_string predicate */
static int test_is_heap_string_predicate(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal heap_str = jacl_intern(&heap, &table, "long string here", 16);
  JaclVal inline_str = jacl_inline_string("short", 5);
  JaclVal integer = jacl_i32(42);
  JaclVal nil = JACL_NIL;

  ASSERT(jacl_is_heap_string(heap_str) == true);
  ASSERT(jacl_is_heap_string(inline_str) == false);
  ASSERT(jacl_is_heap_string(integer) == false);
  ASSERT(jacl_is_heap_string(nil) == false);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-002: Unified string access API ===== */

/* Test: jacl_string_len works for both inline and heap */
static int test_string_len(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal short_s = jacl_inline_string("hello", 5);
  JaclVal empty_s = jacl_inline_string("", 0);
  JaclVal long_s = jacl_intern(&heap, &table, "hello world!", 12);

  ASSERT_U32_EQ(jacl_string_len(short_s), 5);
  ASSERT_U32_EQ(jacl_string_len(empty_s), 0);
  ASSERT_U32_EQ(jacl_string_len(long_s), 12);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_data copies bytes for both types */
static int test_string_data(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal short_s = jacl_inline_string("abc", 3);
  JaclVal long_s = jacl_intern(&heap, &table, "hello world!", 12);

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

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_is_string returns true for both types */
static int test_is_string_unified(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal short_s = jacl_inline_string("hi", 2);
  JaclVal long_s = jacl_intern(&heap, &table, "hello world!", 12);
  JaclVal integer = jacl_i32(42);
  JaclVal nil = JACL_NIL;

  ASSERT(jacl_is_string(short_s) == true);
  ASSERT(jacl_is_string(long_s) == true);
  ASSERT(jacl_is_string(integer) == false);
  ASSERT(jacl_is_string(nil) == false);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
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
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal a = jacl_intern(&heap, &table, "hello world!", 12);
  JaclVal b = jacl_intern(&heap, &table, "hello world!", 12);
  JaclVal c = jacl_intern(&heap, &table, "goodbye world", 13);

  ASSERT(jacl_string_eq(a, b) == true);
  ASSERT(jacl_string_eq(a, c) == false);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_eq — short!=long */
static int test_string_eq_short_long(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal short_s = jacl_inline_string("hello", 5);
  JaclVal long_s = jacl_intern(&heap, &table, "hello world!", 12);

  ASSERT(jacl_string_eq(short_s, long_s) == false);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_eq — cross-representation equality edge case */
static int test_string_eq_cross_rep(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  /* Intern a short string (<=7 bytes) as heap string */
  JaclVal heap_hi = jacl_intern(&heap, &table, "hi", 2);
  JaclVal inline_hi = jacl_inline_string("hi", 2);

  /* Different representations, same content */
  ASSERT(jacl_is_heap_string(heap_hi));
  ASSERT(jacl_is_inline_string(inline_hi));
  ASSERT(jacl_string_eq(heap_hi, inline_hi) == true);
  ASSERT(jacl_string_eq(inline_hi, heap_hi) == true);

  /* Different content */
  JaclVal inline_no = jacl_inline_string("no", 2);
  ASSERT(jacl_string_eq(heap_hi, inline_no) == false);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_cmp — lexicographic ordering */
static int test_string_cmp_ordering(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal abc = jacl_inline_string("abc", 3);
  JaclVal def = jacl_inline_string("def", 3);
  JaclVal ab = jacl_inline_string("ab", 2);
  JaclVal abc2 = jacl_inline_string("abc", 3);
  JaclVal long_abc = jacl_intern(&heap, &table, "abcdefghij", 10);
  JaclVal long_xyz = jacl_intern(&heap, &table, "xyzxyzxyzx", 10);

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

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-003: Compiler support for heap string literals ===== */

/* Helper: compile source with an intern table */
static CompileResult compile_with_intern(const char* source, arena_t* arena,
                                          JaclInternTable* table,
                                          ThreadHeap* heap) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  return compiler_compile(parse, arena, table, heap, NULL);
}

/* Test: short string literal (<=7 bytes) still produces inline constant */
static int test_compile_short_string_still_inline(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  CompileResult cr = compile_with_intern("\"hello\"", &arena, &table, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Should have one constant: an inline string */
  ASSERT(cr.chunk.const_count >= 1);
  JaclVal val = cr.chunk.constants[0];
  ASSERT(jacl_is_inline_string(val));
  ASSERT_U32_EQ(jacl_string_len(val), 5);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: long string literal (>7 bytes) compiles without error, produces heap string */
static int test_compile_long_string_interns(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  CompileResult cr = compile_with_intern("\"hello world\"", &arena, &table, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Should have one constant: a heap string */
  ASSERT(cr.chunk.const_count >= 1);
  JaclVal val = cr.chunk.constants[0];
  ASSERT(jacl_is_heap_string(val));

  JaclHeapString* hs = jacl_as_heap_string(val);
  ASSERT_U32_EQ(hs->byte_len, 11);
  ASSERT(memcmp(hs->data, "hello world", 11) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: duplicate long string literals share the same interned pointer */
static int test_compile_dup_long_strings_share_pointer(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  /* Two statements using the same long literal */
  CompileResult cr = compile_with_intern(
      "[print \"hello world\"] [print \"hello world\"]", &arena, &table, &heap);
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

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [print "hello world"] compiles without error (previously rejected) */
static int test_compile_print_long_string_no_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  CompileResult cr = compile_with_intern("[print \"hello world\"]", &arena, &table, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [def greeting "hello world"] compiles without error */
static int test_compile_def_long_string_no_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  CompileResult cr = compile_with_intern(
      "greet = \"hello world\"", &arena, &table, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
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

  vm_destroy(&vm);
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

  vm_destroy(&vm);
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

  vm_destroy(&vm);
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
      "greet = \"hello world\"\n[print $greet]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  vm_destroy(&vm);
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

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-005: String equality and comparison in the VM ===== */

/* Test: [== "abc" "abc"] returns true (inline-to-inline) */
static int test_vm_eq_inline_inline(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [== \"abc\" \"abc\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [== "hello world" "hello world"] returns true (heap-to-heap) */
static int test_vm_eq_heap_heap(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [== \"hello world\" \"hello world\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [== "abc" "def"] returns false */
static int test_vm_eq_different(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [== \"abc\" \"def\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "false\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [== "abc" 42] returns false (type mismatch, not error) */
static int test_vm_eq_string_vs_int(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [== \"abc\" 42]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "false\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [< "abc" "def"] returns true (lexicographic) */
static int test_vm_lt_strings(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [< \"abc\" \"def\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [> "def" "abc"] returns true */
static int test_vm_gt_strings(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [> \"def\" \"abc\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: numeric comparisons still work after string support added */
static int test_vm_numeric_cmp_unchanged(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [< 1 2]]\n[print [> 5 3]]\n[print [== 7 7]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\ntrue\ntrue\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: heap string ordering [< "abc..." "xyz..."] */
static int test_vm_lt_heap_strings(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [< \"abcdefghij\" \"xyzxyzxyzx\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [<= "abc" "abc"] and [>= "abc" "abc"] both true */
static int test_vm_le_ge_strings(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [<= \"abc\" \"abc\"]]\n[print [>= \"abc\" \"abc\"]]",
      &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\ntrue\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-006: String concatenation (OP_CONCAT and concat builtin) ===== */

/* Test: [concat "he" "llo"] produces "hello" (short+short, inline result) */
static int test_concat_short_short(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [concat \"he\" \"llo\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [concat "hello" " world"] produces "hello world" (short+short, heap result) */
static int test_concat_short_short_heap_result(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [concat \"hello\" \" world\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [concat "hello " "world this is long"] (short+long) */
static int test_concat_short_long(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [concat \"hello \" \"world this is long\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world this is long\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [concat "hello world" " goodbye world"] (long+long) */
static int test_concat_long_long(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [concat \"hello world\" \" goodbye world\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world goodbye world\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [concat "a" "b" "c"] variadic (3 args) */
static int test_concat_variadic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [concat \"a\" \"b\" \"c\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "abc\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [concat "" ""] produces empty inline string */
static int test_concat_empty_empty(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [concat \"\" \"\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: concat result is interned (same concat = same pointer) */
static int test_concat_result_interned(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Two identical concats should produce equal strings (heap interned) */
  VMResult result = jacl_run(
      "[print [== [concat \"hello\" \" world\"] [concat \"hello\" \" world\"]]]",
      &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [def s "hello"] [print [concat $s " world"]] end-to-end with variable */
static int test_concat_with_variable(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "s = \"hello\"\n[print [concat $s \" world\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-007: String builtins (length, slice, index) ===== */

/* Test: [length "hello"] returns 5 */
static int test_length_inline(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [length \"hello\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [length ""] returns 0 */
static int test_length_empty(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [length \"\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [length "hello world this is long"] returns correct length for heap string */
static int test_length_heap(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [length \"hello world this is long\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "24\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [index "hello" 1] returns "e" */
static int test_index_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"hello\" 1]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "e\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [index "hello" 0] returns "h" */
static int test_index_zero(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"hello\" 0]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "h\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [index "hello" -1] returns nil (out of bounds) */
static int test_index_negative(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"hello\" [- 0 1]]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [index "hello" 5] returns nil (out of bounds) */
static int test_index_out_of_bounds(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"hello\" 5]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [slice "hello" 1 3] returns "el" */
static int test_slice_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"hello\" 1 3]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "el\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [slice "hello" 2] returns "llo" (one-arg form: from start to end) */
static int test_slice_one_arg(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"hello\" 2]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "llo\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [slice "hello world this is long" 6 11] returns "world" (heap string) */
static int test_slice_heap_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [slice \"hello world this is long\" 6 11]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "world\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [slice "" 0 0] returns "" (empty string) */
static int test_slice_empty(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"\" 0 0]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [slice "hello" 0 5] returns "hello" (full slice) */
static int test_slice_full(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"hello\" 0 5]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [slice "hello" 3 3] returns "" (zero-length slice) */
static int test_slice_zero_length(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"hello\" 3 3]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: slice result is heap-interned when > 7 bytes */
static int test_slice_heap_result(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Slice out 8 bytes → should be heap-interned */
  VMResult result = jacl_run(
      "[print [slice \"hello world this is long\" 0 8]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello wo\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: index works on heap strings */
static int test_index_heap_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [index \"hello world\" 6]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "w\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: boundary indices — slice clamping */
static int test_slice_boundary(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* end > length is clamped to length */
  VMResult result = jacl_run("[print [slice \"hello\" 3 99]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "lo\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-008: OP_TO_STRING value-to-string coercion ===== */

/* Helper: run source, capture output, verify via OP_TO_STRING through print */
/* Note: We test OP_TO_STRING indirectly by using concat with non-string values
   that need coercion. But since OP_TO_STRING is a raw opcode, we test it via
   the VM directly by building bytecode that uses it. Instead, we'll use the
   end-to-end pattern: compile a program that exercises OP_TO_STRING when
   string interpolation calls it (US-009). For now, we test via direct VM
   bytecode construction. */

/* Test: integer to string — 42 becomes "42" */
static int test_to_string_integer(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  /* Build bytecode: push i32(42), OP_TO_STRING, OP_PRINT, OP_HALT */
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  uint16_t idx = chunk_add_constant(&chunk, jacl_i32(42));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write(&chunk, (uint8_t)(idx >> 8), 1);
  chunk_write(&chunk, (uint8_t)(idx & 0xFF), 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: negative integer to string — -7 becomes "-7" */
static int test_to_string_negative_int(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  uint16_t idx = chunk_add_constant(&chunk, jacl_i32(-7));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write(&chunk, (uint8_t)(idx >> 8), 1);
  chunk_write(&chunk, (uint8_t)(idx & 0xFF), 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "-7\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: float to string — 3.14 becomes "3.14" */
static int test_to_string_float(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  uint16_t idx = chunk_add_constant(&chunk, jacl_f32(3.14f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write(&chunk, (uint8_t)(idx >> 8), 1);
  chunk_write(&chunk, (uint8_t)(idx & 0xFF), 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3.14\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: boolean true to string — becomes "true" */
static int test_to_string_true(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  chunk_write(&chunk, OP_TRUE, 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: boolean false to string — becomes "false" */
static int test_to_string_false(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  chunk_write(&chunk, OP_FALSE, 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "false\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nil to string — becomes "nil" */
static int test_to_string_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  chunk_write(&chunk, OP_NIL, 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: string to string — no-op, pushed back unchanged */
static int test_to_string_string_noop(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  JaclVal str = jacl_inline_string("hello", 5);
  uint16_t idx = chunk_add_constant(&chunk, str);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write(&chunk, (uint8_t)(idx >> 8), 1);
  chunk_write(&chunk, (uint8_t)(idx & 0xFF), 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: closure to string — becomes "<proc name>" */
static int test_to_string_closure(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  /* Create a minimal closure with a name */
  JaclClosure* cl = (JaclClosure*)arena_alloc(&arena, sizeof(JaclClosure));
  memset(cl, 0, sizeof(JaclClosure));
  chunk_init(&cl->chunk, &arena);
  cl->name = "foo";
  cl->param_count = 0;
  cl->upvalue_count = 0;
  cl->upvalues = NULL;
  cl->param_names = NULL;

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  uint16_t idx = chunk_add_constant(&chunk, jacl_closure(cl));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write(&chunk, (uint8_t)(idx >> 8), 1);
  chunk_write(&chunk, (uint8_t)(idx & 0xFF), 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "<proc foo>\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: integer result is inline (<=7 bytes) */
static int test_to_string_inline_result(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  uint16_t idx = chunk_add_constant(&chunk, jacl_i32(42));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write(&chunk, (uint8_t)(idx >> 8), 1);
  chunk_write(&chunk, (uint8_t)(idx & 0xFF), 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  /* Check that result is an inline string */
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  VM vm;
  vm_init(&vm, &arena);
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  /* Top of stack should be inline string "42" */
  JaclVal top = vm.stack[vm.stack_top - 1];
  ASSERT(jacl_is_inline_string(top));
  ASSERT_U32_EQ(jacl_string_len(top), 2);
  char buf[8];
  jacl_string_data(top, buf, sizeof(buf));
  ASSERT(memcmp(buf, "42", 2) == 0);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: long closure name produces heap-interned result */
static int test_to_string_heap_result(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclClosure* cl = (JaclClosure*)arena_alloc(&arena, sizeof(JaclClosure));
  memset(cl, 0, sizeof(JaclClosure));
  chunk_init(&cl->chunk, &arena);
  cl->name = "my_long_proc";  /* "<proc my_long_proc>" = 19 chars > 7 */
  cl->param_count = 0;
  cl->upvalue_count = 0;
  cl->upvalues = NULL;
  cl->param_names = NULL;

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  uint16_t idx = chunk_add_constant(&chunk, jacl_closure(cl));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write(&chunk, (uint8_t)(idx >> 8), 1);
  chunk_write(&chunk, (uint8_t)(idx & 0xFF), 1);
  chunk_write(&chunk, OP_TO_STRING, 1);
  chunk_write(&chunk, OP_HALT, 1);

  JaclInternTable table;
  intern_table_init(&table, &arena);

  VM vm;
  vm_init(&vm, &arena);
  vm.intern_table = &table;

  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  JaclVal top = vm.stack[vm.stack_top - 1];
  ASSERT(jacl_is_heap_string(top));
  JaclHeapString* hs = jacl_as_heap_string(top);
  ASSERT(memcmp(hs->data, "<proc my_long_proc>", hs->byte_len) == 0);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-009: String interpolation compilation ===== */

/* Test: [def name "world"] [print "hello $name"] prints 'hello world' */
static int test_interp_variable(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "name = \"world\"\n[print \"hello $name\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: x = 42 [print "x is $x"] prints 'x is 42' */
static int test_interp_int_variable(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "x = 42\n[print \"x is $x\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "x is 42\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [print "sum is $[+ 1 2]"] prints 'sum is 3' */
static int test_interp_command(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print \"sum is $[+ 1 2]\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "sum is 3\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [print "$[+ 1 2] plus $[+ 3 4]"] prints '3 plus 7' */
static int test_interp_multiple_commands(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print \"$[+ 1 2] plus $[+ 3 4]\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3 plus 7\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: adjacent interpolations — [def a "x"] [def b "y"] [print "$a$b"] */
static int test_interp_adjacent(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "a = \"x\"\nb = \"y\"\n[print \"$a$b\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "xy\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: mixed variable and command interpolation */
static int test_interp_mixed(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "n = 10\n[print \"n=$n, n+1=$[+ $n 1]\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "n=10, n+1=11\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: interpolation with only a variable (no literal text) */
static int test_interp_only_var(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "x = \"hi\"\n[print \"$x\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hi\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: interpolation producing a long (heap) string */
static int test_interp_heap_result(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "name = \"JACL\"\n[print \"hello $name world\"]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello JACL world\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-003 (heap-string): grapheme_len field tests ===== */

/* Test: intern a string with combining marks, verify grapheme_len < byte_len */
static int test_heap_string_grapheme_len_combining(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  /* "e\xCC\x81" = U+0065 U+0301 (e + combining acute accent) — 3 bytes, 1 grapheme */
  const char combining[] = "e\xCC\x81 hello!";  /* 10 bytes, 8 graphemes */
  JaclVal v = jacl_intern(&heap, &table, combining, 10);

  ASSERT(jacl_is_heap_string(v));
  JaclHeapString* hs = jacl_as_heap_string(v);
  ASSERT_U32_EQ(hs->byte_len, 10);
  ASSERT_U32_EQ(hs->grapheme_len, 8);  /* e\xCC\x81 = 1 grapheme, then " hello!" = 7 */
  ASSERT(hs->grapheme_len < hs->byte_len);

  /* jacl_string_len returns grapheme count */
  ASSERT_U32_EQ(jacl_string_len(v), 8);
  /* jacl_string_byte_len returns byte count */
  ASSERT_U32_EQ(jacl_string_byte_len(v), 10);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: intern an ASCII string, grapheme_len == byte_len */
static int test_heap_string_grapheme_len_ascii(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v = jacl_intern(&heap, &table, "hello world", 11);
  JaclHeapString* hs = jacl_as_heap_string(v);
  ASSERT_U32_EQ(hs->byte_len, 11);
  ASSERT_U32_EQ(hs->grapheme_len, 11);
  ASSERT_U32_EQ(jacl_string_len(v), 11);
  ASSERT_U32_EQ(jacl_string_byte_len(v), 11);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_byte_len works for inline strings */
static int test_string_byte_len_inline(void) {
  tracker_reset();
  JaclVal v = jacl_inline_string("hello", 5);
  ASSERT_U32_EQ(jacl_string_byte_len(v), 5);

  JaclVal empty = jacl_inline_string("", 0);
  ASSERT_U32_EQ(jacl_string_byte_len(empty), 0);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_byte_len works for heap strings */
static int test_string_byte_len_heap(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v = jacl_intern(&heap, &table, "hello world!", 12);
  ASSERT_U32_EQ(jacl_string_byte_len(v), 12);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_string_len returns grapheme count for inline strings with combining marks */
static int test_string_len_grapheme_inline(void) {
  tracker_reset();
  /* "e\xCC\x81" = 3 bytes but 1 grapheme cluster (e + combining acute) */
  JaclVal v = jacl_inline_string("e\xCC\x81", 3);
  ASSERT_U32_EQ(jacl_string_len(v), 1);     /* 1 grapheme */
  ASSERT_U32_EQ(jacl_string_byte_len(v), 3); /* 3 bytes */

  /* Pure ASCII: grapheme count == byte count */
  JaclVal ascii = jacl_inline_string("abc", 3);
  ASSERT_U32_EQ(jacl_string_len(ascii), 3);

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
    /* US-005: String equality and comparison in the VM */
    { "vm_eq_inline_inline",                   test_vm_eq_inline_inline },
    { "vm_eq_heap_heap",                        test_vm_eq_heap_heap },
    { "vm_eq_different",                        test_vm_eq_different },
    { "vm_eq_string_vs_int",                    test_vm_eq_string_vs_int },
    { "vm_lt_strings",                          test_vm_lt_strings },
    { "vm_gt_strings",                          test_vm_gt_strings },
    { "vm_numeric_cmp_unchanged",               test_vm_numeric_cmp_unchanged },
    { "vm_lt_heap_strings",                     test_vm_lt_heap_strings },
    { "vm_le_ge_strings",                       test_vm_le_ge_strings },
    /* US-006: String concatenation */
    { "concat_short_short",                      test_concat_short_short },
    { "concat_short_short_heap_result",           test_concat_short_short_heap_result },
    { "concat_short_long",                        test_concat_short_long },
    { "concat_long_long",                         test_concat_long_long },
    { "concat_variadic",                          test_concat_variadic },
    { "concat_empty_empty",                       test_concat_empty_empty },
    { "concat_result_interned",                   test_concat_result_interned },
    { "concat_with_variable",                     test_concat_with_variable },
    /* US-007: String builtins (length, slice, index) */
    { "length_inline",                             test_length_inline },
    { "length_empty",                              test_length_empty },
    { "length_heap",                               test_length_heap },
    { "index_basic",                               test_index_basic },
    { "index_zero",                                test_index_zero },
    { "index_negative",                            test_index_negative },
    { "index_out_of_bounds",                       test_index_out_of_bounds },
    { "index_heap_string",                         test_index_heap_string },
    { "slice_basic",                               test_slice_basic },
    { "slice_one_arg",                             test_slice_one_arg },
    { "slice_heap_string",                         test_slice_heap_string },
    { "slice_empty",                               test_slice_empty },
    { "slice_full",                                test_slice_full },
    { "slice_zero_length",                         test_slice_zero_length },
    { "slice_heap_result",                         test_slice_heap_result },
    { "slice_boundary",                            test_slice_boundary },
    /* US-008: OP_TO_STRING value-to-string coercion */
    { "to_string_integer",                          test_to_string_integer },
    { "to_string_negative_int",                     test_to_string_negative_int },
    { "to_string_float",                            test_to_string_float },
    { "to_string_true",                             test_to_string_true },
    { "to_string_false",                            test_to_string_false },
    { "to_string_nil",                              test_to_string_nil },
    { "to_string_string_noop",                      test_to_string_string_noop },
    { "to_string_closure",                          test_to_string_closure },
    { "to_string_inline_result",                    test_to_string_inline_result },
    { "to_string_heap_result",                      test_to_string_heap_result },
    /* US-003: JaclHeapString with grapheme_len */
    { "heap_string_grapheme_len_combining",           test_heap_string_grapheme_len_combining },
    { "heap_string_grapheme_len_ascii",               test_heap_string_grapheme_len_ascii },
    { "string_byte_len_inline",                       test_string_byte_len_inline },
    { "string_byte_len_heap",                         test_string_byte_len_heap },
    { "string_len_grapheme_inline",                   test_string_len_grapheme_inline },
    /* US-009: String interpolation compilation */
    { "interp_variable",                             test_interp_variable },
    { "interp_int_variable",                         test_interp_int_variable },
    { "interp_command",                              test_interp_command },
    { "interp_multiple_commands",                    test_interp_multiple_commands },
    { "interp_adjacent",                             test_interp_adjacent },
    { "interp_mixed",                                test_interp_mixed },
    { "interp_only_var",                             test_interp_only_var },
    { "interp_heap_result",                          test_interp_heap_result },
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
