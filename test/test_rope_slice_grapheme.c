#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-011: Rope slice grapheme safety ===== */

/* --- Build a rope >128 bytes from multi-codepoint graphemes ---
 *
 * We use a mix of:
 *  - ZWJ emoji sequences (family: U+1F468 U+200D U+1F469 U+200D U+1F467 = 18 bytes)
 *  - Regional indicator pairs (flags: U+1F1FA U+1F1F8 = 8 bytes)
 *  - Combining mark sequences (e + U+0301 acute = 3 bytes)
 *
 * We repeat enough to exceed 128 bytes so the rope uses internal nodes.
 */

/* Man + ZWJ + Woman + ZWJ + Girl = 1 grapheme, 18 bytes */
static const uint8_t ZWJ_FAMILY[] = {
    0xF0, 0x9F, 0x91, 0xA8,  /* U+1F468 */
    0xE2, 0x80, 0x8D,        /* U+200D ZWJ */
    0xF0, 0x9F, 0x91, 0xA9,  /* U+1F469 */
    0xE2, 0x80, 0x8D,        /* U+200D ZWJ */
    0xF0, 0x9F, 0x91, 0xA7   /* U+1F467 */
};
#define ZWJ_FAMILY_LEN 18

/* US flag: U+1F1FA U+1F1F8 = 1 grapheme, 8 bytes */
static const uint8_t FLAG_US[] = {
    0xF0, 0x9F, 0x87, 0xBA,  /* U+1F1FA */
    0xF0, 0x9F, 0x87, 0xB8   /* U+1F1F8 */
};
#define FLAG_US_LEN 8

/* e + combining acute: U+0065 U+0301 = 1 grapheme, 3 bytes */
static const uint8_t E_ACUTE[] = {
    0x65,              /* U+0065 'e' */
    0xCC, 0x81         /* U+0301 combining acute */
};
#define E_ACUTE_LEN 3

/* One cycle = 18 + 8 + 3 = 29 bytes, 3 graphemes
 * 5 cycles = 145 bytes, 15 graphemes → rope territory */
#define PATTERN_CYCLE_LEN (ZWJ_FAMILY_LEN + FLAG_US_LEN + E_ACUTE_LEN)
#define N_CYCLES 5
#define TEST_DATA_LEN (PATTERN_CYCLE_LEN * N_CYCLES)
#define TEST_GRAPHEMES (3 * N_CYCLES)

static void build_test_data(uint8_t *buf) {
    size_t off = 0;
    for (int c = 0; c < N_CYCLES; c++) {
        memcpy(buf + off, ZWJ_FAMILY, ZWJ_FAMILY_LEN); off += ZWJ_FAMILY_LEN;
        memcpy(buf + off, FLAG_US, FLAG_US_LEN);        off += FLAG_US_LEN;
        memcpy(buf + off, E_ACUTE, E_ACUTE_LEN);        off += E_ACUTE_LEN;
    }
}

/* Helper: set up VM and GC heap for rope allocation */
static void setup(arena_t *arena, VM *vm) {
    tracker_reset();
    memset(arena, 0, sizeof(*arena));
    vm_init(vm, arena);
}

static void teardown(arena_t *arena, VM *vm) {
    vm_destroy(vm);
    arena_destroy(arena);
}

/* --- Test 1: Rope contains multi-codepoint graphemes --- */

static int test_rope_multi_codepoint_graphemes(void) {
    arena_t arena;
    VM vm;
    setup(&arena, &vm);

    uint8_t data[TEST_DATA_LEN];
    build_test_data(data);

    rope r = rope_from_str(data, TEST_DATA_LEN);

    ASSERT_SIZE_EQ(rope_byte_count(r), TEST_DATA_LEN);
    ASSERT_SIZE_EQ(rope_grapheme_count(r), TEST_GRAPHEMES);

    teardown(&arena, &vm);
    TEST_PASS();
}

/* --- Test 2: rope_slice_by_graphemes at each offset gives complete graphemes --- */

static int test_slice_single_grapheme_completeness(void) {
    arena_t arena;
    VM vm;
    setup(&arena, &vm);

    uint8_t data[TEST_DATA_LEN];
    build_test_data(data);

    rope r = rope_from_str(data, TEST_DATA_LEN);
    size_t total_graphemes = rope_grapheme_count(r);

    for (size_t g = 0; g < total_graphemes; g++) {
        rope slice = rope_slice_by_graphemes(r, g, 1);
        size_t slice_bytes = rope_byte_count(slice);
        size_t slice_graphemes = rope_grapheme_count(slice);

        /* Single-grapheme slice should have exactly 1 grapheme */
        ASSERT(slice_bytes > 0);
        ASSERT_SIZE_EQ(slice_graphemes, 1);

        /* Verify the first byte is not a UTF-8 continuation byte */
        uint8_t first_byte;
        rope_to_str(slice, &first_byte, 1);
        ASSERT((first_byte & 0xC0) != 0x80);
    }

    teardown(&arena, &vm);
    TEST_PASS();
}

/* --- Test 3: Concatenating all single-grapheme slices reproduces original --- */

static int test_grapheme_slices_concat_to_original(void) {
    arena_t arena;
    VM vm;
    setup(&arena, &vm);

    uint8_t data[TEST_DATA_LEN];
    build_test_data(data);

    rope r = rope_from_str(data, TEST_DATA_LEN);
    size_t total_graphemes = rope_grapheme_count(r);

    /* Concatenate all single-grapheme slices */
    rope result = rope_new();
    for (size_t g = 0; g < total_graphemes; g++) {
        rope slice = rope_slice_by_graphemes(r, g, 1);
        result = rope_concat(result, slice);
    }

    /* Verify the concatenation matches the original */
    ASSERT_SIZE_EQ(rope_byte_count(result), TEST_DATA_LEN);
    ASSERT_SIZE_EQ(rope_grapheme_count(result), TEST_GRAPHEMES);

    /* Compare bytes */
    uint8_t result_buf[TEST_DATA_LEN];
    rope_to_str(result, result_buf, TEST_DATA_LEN);
    ASSERT(memcmp(result_buf, data, TEST_DATA_LEN) == 0);

    teardown(&arena, &vm);
    TEST_PASS();
}

/* --- Test 4: rope_slice at raw byte offsets rejects mid-codepoint splits --- */

static int test_byte_offset_slice_safety(void) {
    arena_t arena;
    VM vm;
    setup(&arena, &vm);

    uint8_t data[TEST_DATA_LEN];
    build_test_data(data);

    rope r = rope_from_str(data, TEST_DATA_LEN);
    size_t total_bytes = rope_byte_count(r);

    for (size_t offset = 0; offset < total_bytes; offset++) {
        size_t remaining = total_bytes - offset;
        rope slice = rope_slice(r, offset, remaining);
        size_t slice_bytes = rope_byte_count(slice);

        if (slice_bytes == 0) {
            /* rope_slice rejected this offset — it's mid-codepoint.
             * Verify that the byte at this offset IS a continuation byte. */
            ASSERT(offset > 0);
            ASSERT(utf8_is_continuation(data[offset]));
        } else {
            /* Accepted — the slice starts on a codepoint boundary */
            uint8_t first_byte;
            rope_to_str(slice, &first_byte, 1);
            ASSERT((first_byte & 0xC0) != 0x80);
        }
    }

    teardown(&arena, &vm);
    TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
    test_fn tests[] = {
        test_rope_multi_codepoint_graphemes,
        test_slice_single_grapheme_completeness,
        test_grapheme_slices_concat_to_original,
        test_byte_offset_slice_safety,
    };

    const char *names[] = {
        "test_rope_multi_codepoint_graphemes",
        "test_slice_single_grapheme_completeness",
        "test_grapheme_slices_concat_to_original",
        "test_byte_offset_slice_safety",
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int pass = 0, fail = 0;

    printf("=== US-011: Rope slice grapheme safety ===\n");
    for (int i = 0; i < n; i++) {
        printf("  %-50s ", names[i]);
        if (tests[i]()) {
            pass++;
        } else {
            printf("FAIL\n");
            fail++;
        }
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
