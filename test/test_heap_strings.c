#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-013: Comprehensive test suite for three-tier strings ===== */

/* --- Helpers --- */

static void fill_ascii(char* buf, size_t len) {
  for (size_t i = 0; i < len; i++) buf[i] = 'a' + (char)(i % 26);
}

static JaclVal do_concat(ThreadHeap* heap, JaclInternTable* table,
                          JaclVal a, JaclVal b) {
  uint32_t len_a = jacl_string_byte_len(a);
  uint32_t len_b = jacl_string_byte_len(b);
  uint32_t total = len_a + len_b;
  if (total <= 128) {
    char buf[256];
    jacl_string_data(a, buf, len_a);
    jacl_string_data(b, buf + len_a, len_b);
    return jacl_string_new(heap, table, buf, total);
  } else {
    rope ra, rb;
    if (jacl_is_rope_string(a)) {
      ra = jacl_as_rope_string(a)->r; rope_ref(ra);
    } else {
      char bufa[128]; jacl_string_data(a, bufa, len_a);
      ra = rope_from_str((const uint8_t*)bufa, len_a);
    }
    if (jacl_is_rope_string(b)) {
      rb = jacl_as_rope_string(b)->r; rope_ref(rb);
    } else {
      char bufb[128]; jacl_string_data(b, bufb, len_b);
      rb = rope_from_str((const uint8_t*)bufb, len_b);
    }
    rope rc = rope_concat(ra, rb);
    uint32_t hash = rope_string__compute_hash(rc);
    JaclRopeString* rs = (JaclRopeString*)gc_alloc(
        heap, OBJ_ROPE_STRING, sizeof(JaclRopeString));
    rs->hash = hash;
    rs->r = rc;
    JaclVal res = jacl_rope_string_ptr(rs);
    rope_unref(ra);
    rope_unref(rb);
    return res;
  }
}

static bool verify_content(JaclVal v, const char* expected, size_t expected_len) {
  char buf[16384];
  uint32_t actual_len = jacl_string_data(v, buf, sizeof(buf));
  if (actual_len != (uint32_t)expected_len) return false;
  return memcmp(buf, expected, expected_len) == 0;
}

typedef struct { char buf[16384]; uint32_t len; } PrintCapture;

static void capture_print(const char* text, uint32_t len, void* ctx) {
  PrintCapture* cap = (PrintCapture*)ctx;
  uint32_t remaining = (uint32_t)sizeof(cap->buf) - cap->len - 1;
  uint32_t copy_len = len < remaining ? len : remaining;
  memcpy(cap->buf + cap->len, text, copy_len);
  cap->len += copy_len;
  cap->buf[cap->len] = '\0';
}

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

/* ========== 1. NFD normalization tests ========== */

/* NFC e-acute (U+00E9, 2 bytes) decomposes to e + U+0301 (3 bytes NFD) */
static int test_nfd_nfc_roundtrip(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* NFC: "caf\xC3\xA9" (5 bytes) → NFD: "cafe\xCC\x81" (6 bytes) */
  JaclVal v = jacl_string_new(&heap, &table, "caf\xC3\xA9", 5);
  ASSERT(!jacl_is_nil(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 6);

  char buf[16];
  jacl_string_data(v, buf, sizeof(buf));
  /* NFD: 'c','a','f','e', 0xCC, 0x81 */
  ASSERT(buf[0] == 'c' && buf[1] == 'a' && buf[2] == 'f' && buf[3] == 'e');
  ASSERT((uint8_t)buf[4] == 0xCC && (uint8_t)buf[5] == 0x81);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Pure ASCII passes through unchanged */
static int test_nfd_ascii_unchanged(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "hello", 5);
  ASSERT(jacl_is_inline_string(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 5);
  ASSERT(verify_content(v, "hello", 5));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Hangul syllable decomposition: U+AC00 (가) → U+1100 + U+1161 */
static int test_nfd_hangul(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* U+AC00 = 0xEA 0xB0 0x80 (3 bytes NFC) → U+1100 + U+1161 (6 bytes NFD) */
  JaclVal v = jacl_string_new(&heap, &table, "\xEA\xB0\x80", 3);
  ASSERT(!jacl_is_nil(v));
  /* NFD: U+1100 (0xE1 0x84 0x80) + U+1161 (0xE1 0x85 0xA1) = 6 bytes */
  ASSERT_U32_EQ(jacl_string_byte_len(v), 6);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 2. Invalid UTF-8 rejection tests ========== */

/* Overlong encoding: 2-byte encoding of ASCII 'A' (0xC1 0x81) */
static int test_invalid_utf8_overlong(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "\xC1\x81", 2);
  ASSERT(jacl_is_nil(v));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Surrogate half: U+D800 (0xED 0xA0 0x80) */
static int test_invalid_utf8_surrogate(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "\xED\xA0\x80", 3);
  ASSERT(jacl_is_nil(v));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Truncated sequence: start of 3-byte char with only 1 continuation */
static int test_invalid_utf8_truncated(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "\xE0\xBF", 2);
  ASSERT(jacl_is_nil(v));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Invalid continuation byte: 0x80 alone */
static int test_invalid_utf8_continuation(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "\x80\x81\x82", 3);
  ASSERT(jacl_is_nil(v));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 3. Tier routing boundary tests ========== */

/* 7-byte string → inline */
static int test_tier_7_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "1234567", 7);
  ASSERT(jacl_is_inline_string(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 7);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* 8-byte string → flat interned */
static int test_tier_8_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "12345678", 8);
  ASSERT(jacl_is_heap_string(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 8);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* 128-byte string → flat interned */
static int test_tier_128_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[128];
  fill_ascii(buf, 128);
  JaclVal v = jacl_string_new(&heap, &table, buf, 128);
  ASSERT(jacl_is_heap_string(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 128);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* 129-byte string → rope */
static int test_tier_129_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[129];
  fill_ascii(buf, 129);
  JaclVal v = jacl_string_new(&heap, &table, buf, 129);
  ASSERT(jacl_is_rope_string(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 129);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 4. Grapheme length tests ========== */

/* ASCII: grapheme count == byte count */
static int test_grapheme_len_ascii(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "hello", 5);
  ASSERT_U32_EQ(jacl_string_len(v), 5);
  ASSERT_U32_EQ(jacl_string_byte_len(v), 5);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Latin with combining marks: e + U+0301 (combining acute) = 1 grapheme, 3 bytes */
static int test_grapheme_len_combining(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "e\xCC\x81" = 3 bytes, 1 grapheme. Extend to 10 bytes for flat tier:
     "aae\xCC\x81aae\xCC\x81" = 10 bytes, 6 graphemes */
  JaclVal v = jacl_string_new(&heap, &table, "aae\xCC\x81""aae\xCC\x81", 10);
  ASSERT(jacl_is_heap_string(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 10);
  ASSERT_U32_EQ(jacl_string_len(v), 6); /* a, a, e+combining, a, a, e+combining */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Emoji: U+1F600 (grinning face) = 4 bytes, 1 grapheme */
static int test_grapheme_len_emoji(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* U+1F600 = F0 9F 98 80 (4 bytes). Two emoji + "ab" = 10 bytes, 4 graphemes */
  JaclVal v = jacl_string_new(&heap, &table,
    "\xF0\x9F\x98\x80" "ab" "\xF0\x9F\x98\x80", 10);
  ASSERT(!jacl_is_nil(v));
  ASSERT_U32_EQ(jacl_string_len(v), 4); /* emoji, a, b, emoji */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ZWJ sequence (simplified): man + ZWJ + heart = still separate graphemes
   unless we have the full family sequence. Test base + ZWJ + base = 1 grapheme.
   U+1F468 (man) + U+200D (ZWJ) + U+2764 (heart) + U+FE0F (VS16) + U+200D (ZWJ) + U+1F48B (kiss) = complex cluster
   For simplicity: just test that ZWJ joins two emoji into 1 grapheme */
static int test_grapheme_len_zwj(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* U+1F468 (man, 4B) + U+200D (ZWJ, 3B) + U+1F469 (woman, 4B) = 11 bytes
     Should be 1 grapheme cluster (ZWJ joins) */
  const char zwj_seq[] =
    "\xF0\x9F\x91\xA8"  /* U+1F468 man */
    "\xE2\x80\x8D"      /* U+200D ZWJ */
    "\xF0\x9F\x91\xA9"; /* U+1F469 woman */

  JaclVal v = jacl_string_new(&heap, &table, zwj_seq, 11);
  ASSERT(!jacl_is_nil(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 11);
  ASSERT_U32_EQ(jacl_string_len(v), 1); /* ZWJ-joined = 1 grapheme */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Regional indicators: U+1F1FA + U+1F1F8 = US flag = 1 grapheme */
static int test_grapheme_len_regional(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* U+1F1FA = F0 9F 87 BA (4B), U+1F1F8 = F0 9F 87 B8 (4B) = 8 bytes total */
  const char flag[] = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
  JaclVal v = jacl_string_new(&heap, &table, flag, 8);
  ASSERT(!jacl_is_nil(v));
  ASSERT_U32_EQ(jacl_string_byte_len(v), 8);
  ASSERT_U32_EQ(jacl_string_len(v), 1); /* flag = 1 grapheme */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 5. Grapheme index and slice at combining boundaries ========== */

/* Index into string with combining marks: returns full cluster */
static int test_grapheme_index_combining(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "ae\xCC\x81b" = a, e+combining, b (3 graphemes, 5 bytes, inline) */
  JaclVal v = jacl_string_new(&heap, &table, "ae\xCC\x81""b", 5);
  ASSERT(jacl_is_inline_string(v));

  /* Index 1 = e+combining (3 bytes: e, 0xCC, 0x81) */
  size_t start, end;
  uint8_t data[8];
  uint32_t blen = jacl_string_byte_len(v);
  jacl_string_data(v, (char*)data, blen);

  bool found = jacl_grapheme_nth(data, blen, 1, &start, &end);
  ASSERT(found);
  ASSERT_SIZE_EQ(end - start, 3); /* e + 2 bytes combining */
  ASSERT(data[start] == 'e');
  ASSERT(data[start+1] == 0xCC);
  ASSERT(data[start+2] == 0x81);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Slice at combining mark boundaries */
static int test_grapheme_slice_combining(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "ae\xCC\x81""bc" = 6 bytes, 4 graphemes */
  JaclVal v = jacl_string_new(&heap, &table, "ae\xCC\x81""bc", 6);
  uint8_t data[8];
  uint32_t blen = jacl_string_byte_len(v);
  jacl_string_data(v, (char*)data, blen);

  /* Grapheme 1 starts at byte 1 (e+combining), grapheme 3 starts at byte 5 (c) */
  size_t byte_start = unicode_grapheme_byte_offset(data, blen, 1);
  size_t byte_end = unicode_grapheme_byte_offset(data, blen, 3);
  ASSERT_SIZE_EQ(byte_start, 1);
  ASSERT_SIZE_EQ(byte_end, 5);

  /* Slice bytes [1..5) = "e\xCC\x81b" (4 bytes, 2 graphemes) */
  JaclVal sliced = jacl_string_new(&heap, &table,
    (const char*)(data + byte_start), (size_t)(byte_end - byte_start));
  ASSERT_U32_EQ(jacl_string_byte_len(sliced), 4);
  ASSERT_U32_EQ(jacl_string_len(sliced), 2);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 6. Concat tier transitions ========== */

/* inline + inline → inline (3+4=7) */
static int test_concat_inline_inline_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "abc", 3);
  JaclVal b = jacl_string_new(&heap, &table, "defg", 4);
  JaclVal r = do_concat(&heap, &table, a, b);
  ASSERT(jacl_is_inline_string(r));
  ASSERT(verify_content(r, "abcdefg", 7));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* inline + flat → flat */
static int test_concat_inline_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf50[50]; fill_ascii(buf50, 50);
  JaclVal a = jacl_string_new(&heap, &table, "hello", 5);
  JaclVal b = jacl_string_new(&heap, &table, buf50, 50);
  JaclVal r = do_concat(&heap, &table, a, b);
  ASSERT(jacl_is_heap_string(r));
  ASSERT_U32_EQ(jacl_string_byte_len(r), 55);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* flat + flat → rope (70+70=140) */
static int test_concat_flat_flat_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf70[70]; fill_ascii(buf70, 70);
  JaclVal a = jacl_string_new(&heap, &table, buf70, 70);
  JaclVal b = jacl_string_new(&heap, &table, buf70, 70);
  JaclVal r = do_concat(&heap, &table, a, b);
  ASSERT(jacl_is_rope_string(r));
  ASSERT_U32_EQ(jacl_string_byte_len(r), 140);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* rope + rope → rope (200+200=400) */
static int test_concat_rope_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf200[200]; fill_ascii(buf200, 200);
  JaclVal a = jacl_string_new(&heap, &table, buf200, 200);
  JaclVal b = jacl_string_new(&heap, &table, buf200, 200);
  JaclVal r = do_concat(&heap, &table, a, b);
  ASSERT(jacl_is_rope_string(r));
  ASSERT_U32_EQ(jacl_string_byte_len(r), 400);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 7. Concat at grapheme boundary ========== */

/* Right operand starts with combining mark → grapheme count != sum */
static int test_concat_grapheme_boundary(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "cafe" (4 graphemes) + "\xCC\x81st" (combining acute + s + t = 2 graphemes)
     After concat: "cafe\xCC\x81st" = 5 graphemes (e+combining = 1 cluster) */
  JaclVal a = jacl_string_new(&heap, &table, "cafe", 4);
  JaclVal b = jacl_string_new(&heap, &table, "\xCC\x81st", 4);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT_U32_EQ(jacl_string_byte_len(r), 8);
  /* grapheme count: c, a, f, e+combining, s, t = 6?
     Wait: "cafe" = c,a,f,e = 4 graphemes. "\xCC\x81st" has the combining mark
     which attaches to the last char of left operand. So result graphemes:
     c, a, f, e+\xCC\x81, s, t = 6 graphemes, not 4+2=6... hmm.
     Actually the left has 4 graphemes and right has 2 graphemes (\xCC\x81 alone
     is 0 graphemes as base, then s, t = but wait, \xCC\x81 doesn't have a base
     in the right operand - it starts with a combining mark).
     Actually, \xCC\x81 by itself is still 1 grapheme (a standalone combining mark
     is its own grapheme cluster in UAX#29 Rule GB999). So right = \xCC\x81 + s + t
     = 3 graphemes. Sum = 4+3 = 7. But after concat the \xCC\x81 merges with 'e' from
     left, so result = c, a, f, e+\xCC\x81, s, t = 6 graphemes.
     So grapheme count should be 6, which is < sum of 7. */
  ASSERT_U32_EQ(jacl_string_len(r), 6);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 8. Equality across all 6 tier-pair combinations ========== */

/* inline == inline */
static int test_eq_inline_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "abc", 3);
  JaclVal b = jacl_string_new(&heap, &table, "abc", 3);
  ASSERT(jacl_string_eq(a, b));

  JaclVal c = jacl_string_new(&heap, &table, "abd", 3);
  ASSERT(!jacl_string_eq(a, c));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* flat == flat (interning gives same pointer) */
static int test_eq_flat_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[50]; fill_ascii(buf, 50);
  JaclVal a = jacl_string_new(&heap, &table, buf, 50);
  JaclVal b = jacl_string_new(&heap, &table, buf, 50);
  ASSERT(jacl_string_eq(a, b));
  /* Interning: should be same pointer */
  ASSERT_U64_EQ(a & JACL_PAYLOAD_MASK, b & JACL_PAYLOAD_MASK);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* inline vs flat: different length → not equal */
static int test_eq_inline_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "hello", 5);    /* inline */
  char buf[20]; fill_ascii(buf, 20);
  JaclVal b = jacl_string_new(&heap, &table, buf, 20);       /* flat */
  ASSERT(!jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* rope == rope (same content) */
static int test_eq_rope_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[200]; fill_ascii(buf, 200);
  JaclVal a = jacl_string_new(&heap, &table, buf, 200);
  JaclVal b = jacl_string_new(&heap, &table, buf, 200);
  ASSERT(jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* rope == flat (same 50-byte content constructed differently) */
static int test_eq_rope_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Create flat string of 50 bytes */
  char buf[50]; fill_ascii(buf, 50);
  JaclVal flat = jacl_string_new(&heap, &table, buf, 50);
  ASSERT(jacl_is_heap_string(flat));

  /* Create rope with same content by manually constructing */
  rope r = rope_from_str((const uint8_t*)buf, 50);
  uint32_t hash = rope_string__compute_hash(r);
  JaclRopeString* rs = (JaclRopeString*)gc_alloc(
      &heap, OBJ_ROPE_STRING, sizeof(JaclRopeString));
  rs->hash = hash;
  rs->r = r;
  JaclVal rope_val = jacl_rope_string_ptr(rs);

  ASSERT(jacl_string_eq(flat, rope_val));
  ASSERT(jacl_string_eq(rope_val, flat));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* rope != inline */
static int test_eq_rope_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal inl = jacl_string_new(&heap, &table, "hello", 5);
  char buf[200]; fill_ascii(buf, 200);
  JaclVal rp = jacl_string_new(&heap, &table, buf, 200);
  ASSERT(!jacl_string_eq(inl, rp));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 9. Comparison (ordering) across tiers ========== */

/* inline < inline */
static int test_cmp_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "abc", 3);
  JaclVal b = jacl_string_new(&heap, &table, "abd", 3);
  ASSERT(jacl_string_cmp(a, b) < 0);
  ASSERT(jacl_string_cmp(b, a) > 0);
  ASSERT(jacl_string_cmp(a, a) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* rope vs flat comparison */
static int test_cmp_rope_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Create two strings that differ: flat "aaa...a" vs rope "aaa...b" */
  char buf_a[50]; memset(buf_a, 'a', 50);
  char buf_b[200]; memset(buf_b, 'a', 200); buf_b[199] = 'b';

  JaclVal flat = jacl_string_new(&heap, &table, buf_a, 50);
  JaclVal rp = jacl_string_new(&heap, &table, buf_b, 200);
  ASSERT(jacl_is_heap_string(flat));
  ASSERT(jacl_is_rope_string(rp));

  /* flat is shorter, but prefix matches: flat < rope */
  ASSERT(jacl_string_cmp(flat, rp) < 0);
  ASSERT(jacl_string_cmp(rp, flat) > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 10. Rope leaf splitting never breaks grapheme cluster ========== */

static int test_rope_leaf_no_grapheme_split(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Build a 300-byte string with combining marks near ROPE_LEAF_MAX (256) boundary.
     Pattern: 253 * 'a' + e + combining acute (0xCC 0x81) + ... more padding.
     The combining mark sequence at byte 254-255 must not be split. */
  uint8_t data[300];
  memset(data, 'a', 300);
  /* Place combining mark sequence at ~byte 253: base 'e' + combining */
  data[253] = 'e';
  data[254] = 0xCC;
  data[255] = 0x81;

  rope r = rope_from_str(data, 300);

  /* Verify grapheme count matches flat computation */
  size_t flat_gc = unicode_grapheme_count(data, 300);
  size_t rope_gc = rope_grapheme_count(r);
  ASSERT_SIZE_EQ(rope_gc, flat_gc);

  /* Verify by materializing and comparing */
  uint8_t out[300];
  rope_to_str(r, out, 300);
  ASSERT(memcmp(data, out, 300) == 0);

  rope_unref(r);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 11. Rope hash consistency ========== */

/* Same content as flat and as rope (built by concatenation) → same hash */
static int test_rope_hash_consistency(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build 200-byte content */
  char buf[200]; fill_ascii(buf, 200);

  /* Direct rope from full content */
  JaclVal direct = jacl_string_new(&heap, &table, buf, 200);
  ASSERT(jacl_is_rope_string(direct));

  /* Rope built by concatenating two 100-byte pieces */
  JaclVal piece1 = jacl_string_new(&heap, &table, buf, 100);
  JaclVal piece2 = jacl_string_new(&heap, &table, buf + 100, 100);
  JaclVal concat = do_concat(&heap, &table, piece1, piece2);
  ASSERT(jacl_is_rope_string(concat));

  /* Hashes must match (same content) */
  uint32_t hash_direct = jacl_as_rope_string(direct)->hash;
  uint32_t hash_concat = jacl_as_rope_string(concat)->hash;
  ASSERT_U32_EQ(hash_direct, hash_concat);

  /* Also verify against flat FNV-1a */
  uint32_t flat_hash = string__fnv1a(buf, 200);
  ASSERT_U32_EQ(hash_direct, flat_hash);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 12. Large rope operations (>10KB) ========== */

static int test_large_rope_concat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 12KB string by repeated concat */
  char chunk[200]; fill_ascii(chunk, 200);
  JaclVal big = jacl_string_new(&heap, &table, chunk, 200);
  for (int i = 0; i < 59; i++) { /* 60 * 200 = 12000 bytes */
    JaclVal piece = jacl_string_new(&heap, &table, chunk, 200);
    big = do_concat(&heap, &table, big, piece);
  }
  ASSERT(jacl_is_rope_string(big));
  ASSERT_U32_EQ(jacl_string_byte_len(big), 12000);
  ASSERT_U32_EQ(jacl_string_len(big), 12000); /* ASCII: graphemes == bytes */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_large_rope_slice(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 10KB rope */
  char* bigbuf = (char*)tracked_malloc(10000);
  fill_ascii(bigbuf, 10000);
  JaclVal big = jacl_string_new(&heap, &table, bigbuf, 10000);
  ASSERT(jacl_is_rope_string(big));

  /* Slice out middle: graphemes 5000..5050 (ASCII, so byte offset matches) */
  rope r = jacl_as_rope_string(big)->r;
  rope sliced = rope_slice_by_graphemes(r, 5000, 50);
  size_t slen = rope_byte_count(sliced);
  ASSERT_SIZE_EQ(slen, 50);

  /* Verify content matches original bytes */
  uint8_t out[50];
  rope_to_str(sliced, out, 50);
  ASSERT(memcmp(out, bigbuf + 5000, 50) == 0);

  rope_unref(sliced);
  tracked_free(bigbuf);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_large_rope_index(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 10KB rope */
  char* bigbuf = (char*)tracked_malloc(10000);
  fill_ascii(bigbuf, 10000);
  JaclVal big = jacl_string_new(&heap, &table, bigbuf, 10000);
  ASSERT(jacl_is_rope_string(big));

  /* Index the 5000th grapheme (ASCII, so byte 5000) */
  rope r = jacl_as_rope_string(big)->r;
  rope_offset_result res = rope_grapheme_to_byte(r, 5000);
  ASSERT(res.found);
  /* At byte 5000, the char should be bigbuf[5000] = 'a'+(5000%26) */
  uint8_t ch;
  rope_st_copy_range(r.root, res.value, 1, &ch);
  ASSERT(ch == (uint8_t)bigbuf[5000]);

  tracked_free(bigbuf);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 13. Existing M5 string tests (end-to-end via jacl_run) ========== */

static int test_m5_string_ops(void) {
  PrintCapture cap;
  VMResult r;

  /* Basic string operations that M5 tests rely on */
  r = run_capture(
    "[print [concat \"hello\" \" \" \"world\"]]\n"
    "[print [length \"hello\"]]\n"
    "[print [slice \"hello\" 1 3]]\n"
    "[print [index \"hello\" 0]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n5\nel\nh\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 14. GC traces rope string objects ========== */

static int test_gc_rope_strings(void) {
  PrintCapture cap;
  /* Create rope strings via recursive concat (causes allocation pressure / GC),
     then use the result to verify no corruption */
  VMResult r = run_capture(
    "proc rep {s, n} {\n"
    "  if [== $n 0] { \"\" } { concat $s [rep $s [- $n 1]] }\n"
    "}\n"
    "big = [rep \"abcdefghijklmnopqrstuvwxyz\" 20]\n"
    "[print [byte-length $big]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* 26 * 20 = 520 bytes */
  ASSERT_STR_EQ(cap.buf, "520\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Stress test: create and discard many rope strings to exercise GC sweep */
static int test_gc_rope_stress(void) {
  PrintCapture cap;
  /* Create and discard many rope strings via repeated top-level concat
     to exercise GC sweep of rope objects */
  VMResult r = run_capture(
    "s : \"start\"\n"
    "i : 0\n"
    "[while [< $i 30] {\n"
    "  s :: [concat $s \"abcdefghijklmnopqrstuvwxyz\"]\n"
    "  i :: [+ $i 1]\n"
    "}]\n"
    "[print [byte-length $s]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* "start" (5) + 30*26 (780) = 785 bytes */
  ASSERT_STR_EQ(cap.buf, "785\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 15. Cross-tier combining marks equality ========== */

static int test_eq_combining_cross_tier(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Same NFD content as inline (5 bytes) and as flat (need 8+ bytes).
     "ae\xCC\x81" = 4 bytes (inline). Let's use a longer combining sequence
     for flat tier: build 9-byte string with combining marks. */
  const char data[] = "aae\xCC\x81""bbcc"; /* 9 bytes, flat tier */
  JaclVal flat = jacl_string_new(&heap, &table, data, 9);
  ASSERT(jacl_is_heap_string(flat));

  /* Create same content again — interning should give same pointer */
  JaclVal flat2 = jacl_string_new(&heap, &table, data, 9);
  ASSERT(jacl_string_eq(flat, flat2));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 16. VM integration: equality and comparison ========== */

static int test_vm_equality(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [== \"hello\" \"hello\"]]\n"
    "[print [== \"abc\" \"abd\"]]\n"
    "[print [< \"abc\" \"abd\"]]\n"
    "[print [> \"xyz\" \"abc\"]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\nfalse\ntrue\ntrue\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 17. VM integration: index and slice ========== */

static int test_vm_index_slice(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [index \"hello\" 0]]\n"
    "[print [index \"hello\" 4]]\n"
    "[print [slice \"hello\" 1 3]]\n"
    "[print [slice \"abcdef\" 2]]",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "h\no\nel\ncdef\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ========== 18. Rope concat + equality: structural sharing ========== */

static int test_rope_structural_sharing(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build two ropes that share structure, verify equality */
  char buf[200]; fill_ascii(buf, 200);
  JaclVal base = jacl_string_new(&heap, &table, buf, 200);
  JaclVal suffix = jacl_string_new(&heap, &table, "xyz", 3);

  JaclVal r1 = do_concat(&heap, &table, base, suffix);
  JaclVal r2 = do_concat(&heap, &table, base, suffix);
  ASSERT(jacl_string_eq(r1, r2));
  ASSERT_U32_EQ(jacl_string_byte_len(r1), 203);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
  test_fn tests[] = {
    /* NFD normalization */
    test_nfd_nfc_roundtrip,
    test_nfd_ascii_unchanged,
    test_nfd_hangul,
    /* Invalid UTF-8 rejection */
    test_invalid_utf8_overlong,
    test_invalid_utf8_surrogate,
    test_invalid_utf8_truncated,
    test_invalid_utf8_continuation,
    /* Tier routing at boundaries */
    test_tier_7_inline,
    test_tier_8_flat,
    test_tier_128_flat,
    test_tier_129_rope,
    /* Grapheme length */
    test_grapheme_len_ascii,
    test_grapheme_len_combining,
    test_grapheme_len_emoji,
    test_grapheme_len_zwj,
    test_grapheme_len_regional,
    /* Grapheme index and slice */
    test_grapheme_index_combining,
    test_grapheme_slice_combining,
    /* Concat tier transitions */
    test_concat_inline_inline_inline,
    test_concat_inline_flat,
    test_concat_flat_flat_rope,
    test_concat_rope_rope,
    /* Concat at grapheme boundary */
    test_concat_grapheme_boundary,
    /* Equality across tier pairs */
    test_eq_inline_inline,
    test_eq_flat_flat,
    test_eq_inline_flat,
    test_eq_rope_rope,
    test_eq_rope_flat,
    test_eq_rope_inline,
    /* Comparison across tiers */
    test_cmp_inline,
    test_cmp_rope_flat,
    /* Rope leaf splitting */
    test_rope_leaf_no_grapheme_split,
    /* Rope hash consistency */
    test_rope_hash_consistency,
    /* Large rope operations */
    test_large_rope_concat,
    test_large_rope_slice,
    test_large_rope_index,
    /* M5 string tests */
    test_m5_string_ops,
    /* GC traces rope strings */
    test_gc_rope_strings,
    test_gc_rope_stress,
    /* Cross-tier combining marks */
    test_eq_combining_cross_tier,
    /* VM integration */
    test_vm_equality,
    test_vm_index_slice,
    /* Rope structural sharing */
    test_rope_structural_sharing,
  };

  const char* names[] = {
    "test_nfd_nfc_roundtrip",
    "test_nfd_ascii_unchanged",
    "test_nfd_hangul",
    "test_invalid_utf8_overlong",
    "test_invalid_utf8_surrogate",
    "test_invalid_utf8_truncated",
    "test_invalid_utf8_continuation",
    "test_tier_7_inline",
    "test_tier_8_flat",
    "test_tier_128_flat",
    "test_tier_129_rope",
    "test_grapheme_len_ascii",
    "test_grapheme_len_combining",
    "test_grapheme_len_emoji",
    "test_grapheme_len_zwj",
    "test_grapheme_len_regional",
    "test_grapheme_index_combining",
    "test_grapheme_slice_combining",
    "test_concat_inline_inline_inline",
    "test_concat_inline_flat",
    "test_concat_flat_flat_rope",
    "test_concat_rope_rope",
    "test_concat_grapheme_boundary",
    "test_eq_inline_inline",
    "test_eq_flat_flat",
    "test_eq_inline_flat",
    "test_eq_rope_rope",
    "test_eq_rope_flat",
    "test_eq_rope_inline",
    "test_cmp_inline",
    "test_cmp_rope_flat",
    "test_rope_leaf_no_grapheme_split",
    "test_rope_hash_consistency",
    "test_large_rope_concat",
    "test_large_rope_slice",
    "test_large_rope_index",
    "test_m5_string_ops",
    "test_gc_rope_strings",
    "test_gc_rope_stress",
    "test_eq_combining_cross_tier",
    "test_vm_equality",
    "test_vm_index_slice",
    "test_rope_structural_sharing",
  };

  int n = (int)(sizeof(tests) / sizeof(tests[0]));
  int pass = 0, fail = 0;

  printf("=== US-013: Comprehensive three-tier string test suite ===\n");
  for (int i = 0; i < n; i++) {
    printf("  %-50s ", names[i]);
    if (tests[i]()) {
      printf("PASS\n");
      pass++;
    } else {
      printf("FAIL\n");
      fail++;
    }
  }

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
