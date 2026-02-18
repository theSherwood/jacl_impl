#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "../test/test_helpers.h"

#define VALUE_IMPLEMENTATION
#include "./value.h"

/* Test: JaclVal is uint64_t (8 bytes) */
static int test_jaclval_size(void) {
  ASSERT(sizeof(JaclVal) == 8);
  TEST_PASS();
}

/* Test: tag byte shift and masks */
static int test_tag_layout(void) {
  /* Tag byte occupies bits 63-56 */
  ASSERT(JACL_TAG_SHIFT == 56);

  /* Tag mask should select only the top byte */
  ASSERT_U64_EQ(JACL_TAG_MASK, UINT64_C(0xFF00000000000000));

  /* Payload mask should select the low 56 bits */
  ASSERT_U64_EQ(JACL_PAYLOAD_MASK, UINT64_C(0x00FFFFFFFFFFFFFF));

  /* Tag and payload masks should be complementary */
  ASSERT_U64_EQ(JACL_TAG_MASK | JACL_PAYLOAD_MASK, UINT64_MAX);
  ASSERT_U64_EQ(JACL_TAG_MASK & JACL_PAYLOAD_MASK, 0);

  TEST_PASS();
}

/* Test: flag bit positions */
static int test_flag_bits(void) {
  /* Tainted = bit 7 of tag byte = bit 63 */
  ASSERT_U64_EQ(JACL_FLAG_TAINTED, UINT64_C(1) << 63);

  /* Secret = bit 6 of tag byte = bit 62 */
  ASSERT_U64_EQ(JACL_FLAG_SECRET, UINT64_C(1) << 62);

  /* Error = bit 5 of tag byte = bit 61 */
  ASSERT_U64_EQ(JACL_FLAG_ERROR, UINT64_C(1) << 61);

  /* Flags mask is the OR of all three */
  ASSERT_U64_EQ(JACL_FLAGS_MASK, JACL_FLAG_TAINTED | JACL_FLAG_SECRET | JACL_FLAG_ERROR);

  /* Flags should not overlap with type tag or payload */
  ASSERT_U64_EQ(JACL_FLAGS_MASK & JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_FLAGS_MASK & JACL_PAYLOAD_MASK, 0);

  TEST_PASS();
}

/* Test: type tag mask */
static int test_type_mask(void) {
  /* Type mask should select low 5 bits of tag byte (bits 60-56) */
  ASSERT_U64_EQ(JACL_TYPE_MASK, UINT64_C(0x1F) << 56);

  /* Type mask + flags mask should cover the entire tag byte */
  ASSERT_U64_EQ(JACL_TYPE_MASK | JACL_FLAGS_MASK, JACL_TAG_MASK);

  TEST_PASS();
}

/* Test: type tag constants are distinct and in the tag byte */
static int test_type_tags(void) {
  /* All type tags must be within the type mask */
  ASSERT_U64_EQ(JACL_TAG_NIL & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_BOOL & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_I32 & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_F32 & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_INLINE_STRING & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_STRING & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_VECTOR & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_MAP & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_CLOSURE & ~JACL_TYPE_MASK, 0);
  ASSERT_U64_EQ(JACL_TAG_BIGNUM & ~JACL_TYPE_MASK, 0);

  /* All type tags must be distinct */
  uint64_t tags[] = {
    JACL_TAG_NIL, JACL_TAG_BOOL, JACL_TAG_I32, JACL_TAG_F32,
    JACL_TAG_INLINE_STRING, JACL_TAG_STRING, JACL_TAG_VECTOR,
    JACL_TAG_MAP, JACL_TAG_CLOSURE, JACL_TAG_BIGNUM
  };
  size_t n = sizeof(tags) / sizeof(tags[0]);
  for (size_t i = 0; i < n; i++) {
    for (size_t j = i + 1; j < n; j++) {
      if (tags[i] == tags[j]) {
        fprintf(stderr, "FAIL: type tags %zu and %zu are equal\n", i, j);
        return 0;
      }
    }
  }

  /* NIL tag should be zero (in the type bits) */
  ASSERT_U64_EQ(JACL_TAG_NIL, 0);

  TEST_PASS();
}

/* Test: tag byte does not overlap payload */
static int test_tag_payload_separation(void) {
  /* A value with all payload bits set and NIL tag should have zero in tag area */
  JaclVal v = JACL_PAYLOAD_MASK;
  ASSERT_U64_EQ(v & JACL_TAG_MASK, 0);

  /* A value with BOOL tag should have zero in payload area */
  JaclVal w = JACL_TAG_BOOL;
  ASSERT_U64_EQ(w & JACL_PAYLOAD_MASK, 0);

  /* Combining tag and payload should be lossless */
  JaclVal combined = JACL_TAG_I32 | UINT64_C(0x00DEADBEEFCAFE);
  ASSERT_U64_EQ(combined & JACL_TAG_MASK, JACL_TAG_I32);
  ASSERT_U64_EQ(combined & JACL_PAYLOAD_MASK, UINT64_C(0x00DEADBEEFCAFE));

  TEST_PASS();
}

/* ---- US-002 tests: Nil and boolean constructors, extractors, predicates ---- */

/* Test: JACL_NIL is nil-tagged with zero payload */
static int test_nil_constant(void) {
  ASSERT_U64_EQ(JACL_NIL & JACL_TYPE_MASK, JACL_TAG_NIL);
  ASSERT_U64_EQ(JACL_NIL & JACL_PAYLOAD_MASK, 0);
  ASSERT_U64_EQ(JACL_NIL, 0);
  TEST_PASS();
}

/* Test: JACL_TRUE and JACL_FALSE are bool-tagged */
static int test_bool_constants(void) {
  /* Both should be bool-tagged */
  ASSERT_U64_EQ(JACL_TRUE & JACL_TYPE_MASK, JACL_TAG_BOOL);
  ASSERT_U64_EQ(JACL_FALSE & JACL_TYPE_MASK, JACL_TAG_BOOL);

  /* TRUE has payload 1, FALSE has payload 0 */
  ASSERT_U64_EQ(JACL_TRUE & JACL_PAYLOAD_MASK, 1);
  ASSERT_U64_EQ(JACL_FALSE & JACL_PAYLOAD_MASK, 0);

  /* TRUE != FALSE */
  ASSERT(JACL_TRUE != JACL_FALSE);

  TEST_PASS();
}

/* Test: jacl_bool constructor */
static int test_jacl_bool(void) {
  JaclVal t = jacl_bool(true);
  JaclVal f = jacl_bool(false);

  ASSERT_U64_EQ(t, JACL_TRUE);
  ASSERT_U64_EQ(f, JACL_FALSE);

  TEST_PASS();
}

/* Test: jacl_is_nil predicate */
static int test_is_nil(void) {
  ASSERT(jacl_is_nil(JACL_NIL));

  /* Bool values are not nil */
  ASSERT(!jacl_is_nil(JACL_TRUE));
  ASSERT(!jacl_is_nil(JACL_FALSE));

  /* Nil with flags set is still nil */
  ASSERT(jacl_is_nil(JACL_NIL | JACL_FLAG_TAINTED));
  ASSERT(jacl_is_nil(JACL_NIL | JACL_FLAG_SECRET));
  ASSERT(jacl_is_nil(JACL_NIL | JACL_FLAG_ERROR));
  ASSERT(jacl_is_nil(JACL_NIL | JACL_FLAGS_MASK));

  /* Other type tags are not nil */
  ASSERT(!jacl_is_nil(JACL_TAG_BOOL));
  ASSERT(!jacl_is_nil(JACL_TAG_I32));
  ASSERT(!jacl_is_nil(JACL_TAG_F32));

  TEST_PASS();
}

/* Test: jacl_is_bool predicate */
static int test_is_bool(void) {
  ASSERT(jacl_is_bool(JACL_TRUE));
  ASSERT(jacl_is_bool(JACL_FALSE));
  ASSERT(jacl_is_bool(jacl_bool(true)));
  ASSERT(jacl_is_bool(jacl_bool(false)));

  /* Nil is not bool */
  ASSERT(!jacl_is_bool(JACL_NIL));

  /* Bool with flags set is still bool */
  ASSERT(jacl_is_bool(JACL_TRUE | JACL_FLAG_TAINTED));
  ASSERT(jacl_is_bool(JACL_FALSE | JACL_FLAG_ERROR));
  ASSERT(jacl_is_bool(JACL_TRUE | JACL_FLAGS_MASK));

  /* Other type tags are not bool */
  ASSERT(!jacl_is_bool(JACL_TAG_I32));
  ASSERT(!jacl_is_bool(JACL_TAG_F32));
  ASSERT(!jacl_is_bool(JACL_TAG_NIL));

  TEST_PASS();
}

/* Test: jacl_as_bool extractor */
static int test_as_bool(void) {
  /* Round-trip: jacl_as_bool(jacl_bool(x)) == x */
  ASSERT(jacl_as_bool(jacl_bool(true)) == true);
  ASSERT(jacl_as_bool(jacl_bool(false)) == false);

  /* Constants */
  ASSERT(jacl_as_bool(JACL_TRUE) == true);
  ASSERT(jacl_as_bool(JACL_FALSE) == false);

  TEST_PASS();
}

/* Test: nil is not bool, bool is not nil (cross-type) */
static int test_nil_bool_distinction(void) {
  /* nil is not bool */
  ASSERT(!jacl_is_bool(JACL_NIL));
  /* bool is not nil */
  ASSERT(!jacl_is_nil(JACL_TRUE));
  ASSERT(!jacl_is_nil(JACL_FALSE));
  /* TRUE != FALSE */
  ASSERT(JACL_TRUE != JACL_FALSE);

  TEST_PASS();
}

/* ---- US-003 tests: i32 constructors and extractors ---- */

/* Test: jacl_i32 constructor produces i32-tagged values */
static int test_i32_constructor(void) {
  JaclVal v0 = jacl_i32(0);
  JaclVal v1 = jacl_i32(1);
  JaclVal vn = jacl_i32(-1);

  /* All should be i32-tagged */
  ASSERT_U64_EQ(v0 & JACL_TYPE_MASK, JACL_TAG_I32);
  ASSERT_U64_EQ(v1 & JACL_TYPE_MASK, JACL_TAG_I32);
  ASSERT_U64_EQ(vn & JACL_TYPE_MASK, JACL_TAG_I32);

  TEST_PASS();
}

/* Test: jacl_is_i32 predicate */
static int test_is_i32(void) {
  ASSERT(jacl_is_i32(jacl_i32(0)));
  ASSERT(jacl_is_i32(jacl_i32(42)));
  ASSERT(jacl_is_i32(jacl_i32(-1)));
  ASSERT(jacl_is_i32(jacl_i32(INT32_MAX)));
  ASSERT(jacl_is_i32(jacl_i32(INT32_MIN)));

  /* i32 with flags set is still i32 */
  ASSERT(jacl_is_i32(jacl_i32(7) | JACL_FLAG_TAINTED));
  ASSERT(jacl_is_i32(jacl_i32(7) | JACL_FLAG_SECRET));
  ASSERT(jacl_is_i32(jacl_i32(7) | JACL_FLAG_ERROR));
  ASSERT(jacl_is_i32(jacl_i32(7) | JACL_FLAGS_MASK));

  /* Other types are not i32 */
  ASSERT(!jacl_is_i32(JACL_NIL));
  ASSERT(!jacl_is_i32(JACL_TRUE));
  ASSERT(!jacl_is_i32(JACL_FALSE));
  ASSERT(!jacl_is_i32(JACL_TAG_F32));

  TEST_PASS();
}

/* Test: jacl_as_i32 extractor and round-trip */
static int test_as_i32(void) {
  /* Round-trip: jacl_as_i32(jacl_i32(n)) == n */
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(0)), 0);
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(1)), 1);
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(-1)), -1);
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(INT32_MAX)), INT32_MAX);
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(INT32_MIN)), INT32_MIN);

  /* Additional edge cases */
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(42)), 42);
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(-42)), -42);
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(12345)), 12345);
  ASSERT_INT_EQ(jacl_as_i32(jacl_i32(-12345)), -12345);

  TEST_PASS();
}

/* Test: negative i32 sign handling (no sign extension into tag byte) */
static int test_i32_sign_handling(void) {
  /* Negative values must not bleed into the tag byte */
  JaclVal vn1 = jacl_i32(-1);
  /* -1 as uint32_t is 0xFFFFFFFF, which fits in 56 bits */
  ASSERT_U64_EQ(vn1 & JACL_TAG_MASK, JACL_TAG_I32);
  /* The payload should be 0x00000000FFFFFFFF, not 0x00FFFFFFFFFFFFFF */
  ASSERT_U64_EQ(vn1 & JACL_PAYLOAD_MASK, UINT64_C(0xFFFFFFFF));

  JaclVal vmin = jacl_i32(INT32_MIN);
  ASSERT_U64_EQ(vmin & JACL_TAG_MASK, JACL_TAG_I32);
  /* INT32_MIN = -2147483648, as uint32_t = 0x80000000 */
  ASSERT_U64_EQ(vmin & JACL_PAYLOAD_MASK, UINT64_C(0x80000000));

  /* Round-trip still works */
  ASSERT_INT_EQ(jacl_as_i32(vn1), -1);
  ASSERT_INT_EQ(jacl_as_i32(vmin), INT32_MIN);

  TEST_PASS();
}

int main(void) {
  printf("Test: JaclVal size\n");
  if (!test_jaclval_size()) return 1;

  printf("Test: tag byte layout\n");
  if (!test_tag_layout()) return 1;

  printf("Test: flag bit positions\n");
  if (!test_flag_bits()) return 1;

  printf("Test: type tag mask\n");
  if (!test_type_mask()) return 1;

  printf("Test: type tag constants\n");
  if (!test_type_tags()) return 1;

  printf("Test: tag/payload separation\n");
  if (!test_tag_payload_separation()) return 1;

  /* US-002: Nil and boolean tests */
  printf("Test: nil constant\n");
  if (!test_nil_constant()) return 1;

  printf("Test: bool constants\n");
  if (!test_bool_constants()) return 1;

  printf("Test: jacl_bool constructor\n");
  if (!test_jacl_bool()) return 1;

  printf("Test: jacl_is_nil predicate\n");
  if (!test_is_nil()) return 1;

  printf("Test: jacl_is_bool predicate\n");
  if (!test_is_bool()) return 1;

  printf("Test: jacl_as_bool extractor\n");
  if (!test_as_bool()) return 1;

  printf("Test: nil/bool distinction\n");
  if (!test_nil_bool_distinction()) return 1;

  /* US-003: i32 constructor, predicate, extractor */
  printf("Test: i32 constructor\n");
  if (!test_i32_constructor()) return 1;

  printf("Test: jacl_is_i32 predicate\n");
  if (!test_is_i32()) return 1;

  printf("Test: jacl_as_i32 extractor\n");
  if (!test_as_i32()) return 1;

  printf("Test: i32 sign handling\n");
  if (!test_i32_sign_handling()) return 1;

  return 0;
}
