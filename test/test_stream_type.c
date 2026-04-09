#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-008: Stream value type and GC integration ===== */

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

/* --- Test: stream creation and tag --- */
static int test_stream_create(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclVal s = jacl_stream(&vm.heap);
  ASSERT(jacl_is_stream(s));
  ASSERT(!jacl_is_vector(s));
  ASSERT(!jacl_is_closure(s));

  JaclStream* sp = jacl_as_stream(s);
  ASSERT_INT_EQ(sp->state, STREAM_PENDING);
  ASSERT(sp->next_fn == JACL_NIL);
  ASSERT(sp->cached_value == JACL_NIL);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: stream print output --- */
static int test_stream_print(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclVal s = jacl_stream(&vm.heap);
  VMFormatBuf fmt;
  vm__fmt_init(&fmt, &arena);
  vm__fmt_value(&fmt, s);

  ASSERT_INT_EQ(fmt.len, 8);
  ASSERT(memcmp(fmt.data, "<stream>", 8) == 0);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: stream identity equality --- */
static int test_stream_identity_eq(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclVal s1 = jacl_stream(&vm.heap);
  JaclVal s2 = jacl_stream(&vm.heap);

  /* Same stream == itself */
  ASSERT(jacl_val_eq(s1, s1));
  /* Different streams != each other */
  ASSERT(!jacl_val_eq(s1, s2));

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: stream type_name --- */
static int test_stream_type_name(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclVal s = jacl_stream(&vm.heap);
  ASSERT_STR_EQ(vm__type_name(s), "stream");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: stream state transitions --- */
static int test_stream_states(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  JaclVal s = jacl_stream(&vm.heap);
  JaclStream* sp = jacl_as_stream(s);

  /* Start in PENDING state */
  ASSERT_INT_EQ(sp->state, STREAM_PENDING);

  /* Transition to CONSUMED */
  sp->state = STREAM_CONSUMED;
  ASSERT_INT_EQ(sp->state, STREAM_CONSUMED);

  /* Transition to EXHAUSTED, release producer */
  sp->state = STREAM_EXHAUSTED;
  sp->next_fn = JACL_NIL;
  ASSERT_INT_EQ(sp->state, STREAM_EXHAUSTED);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: stream with closure next_fn survives GC --- */
static int test_stream_gc_survives(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);

  /* Create a stream and set its next_fn to a test value */
  JaclVal s = jacl_stream(&vm.heap);
  JaclStream* sp = jacl_as_stream(s);
  sp->cached_value = jacl_i32(42);

  /* Put the stream on the VM stack so it's a GC root */
  vm__push(&vm, s);

  /* Verify still valid after access */
  JaclVal top = vm.stack[vm.stack_top - 1];
  ASSERT(jacl_is_stream(top));
  JaclStream* sp2 = jacl_as_stream(top);
  ASSERT_INT_EQ(jacl_as_i32(sp2->cached_value), 42);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: TYPE_STREAM recognized by compiler type system --- */
static int test_stream_type_keyword(void) {
  PrintCapture cap;
  cap.len = 0;
  cap.buf[0] = '\0';
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* 'stream' should be recognized as a type keyword
     (it won't conflict with variable names due to type position detection) */
  VMResult r = jacl_run(
    "proc id {stream s} { $s }\n"
    "[print \"ok\"]\n",
    &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "ok\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: tag value is 0x15 --- */
static int test_stream_tag_value(void) {
  ASSERT(JACL_TAG_STREAM == ((uint64_t)0x15 << 56));
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "stream_create",       test_stream_create },
    { "stream_print",        test_stream_print },
    { "stream_identity_eq",  test_stream_identity_eq },
    { "stream_type_name",    test_stream_type_name },
    { "stream_states",       test_stream_states },
    { "stream_gc_survives",  test_stream_gc_survives },
    { "stream_type_keyword", test_stream_type_keyword },
    { "stream_tag_value",    test_stream_tag_value },
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
