#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- US-004 tests: f32 constructors and extractors ---- */

/* Test: jacl_f32 constructor produces f32-tagged values */
static int test_f32_constructor(void) {
  JaclVal v0 = jacl_f32(0.0f);
  JaclVal v1 = jacl_f32(1.0f);
  JaclVal vn = jacl_f32(-1.0f);

  ASSERT_U64_EQ(v0 & JACL_TYPE_MASK, JACL_TAG_F32);
  ASSERT_U64_EQ(v1 & JACL_TYPE_MASK, JACL_TAG_F32);
  ASSERT_U64_EQ(vn & JACL_TYPE_MASK, JACL_TAG_F32);

  TEST_PASS();
}

/* Test: jacl_is_f32 predicate */
static int test_is_f32(void) {
  ASSERT(jacl_is_f32(jacl_f32(0.0f)));
  ASSERT(jacl_is_f32(jacl_f32(1.0f)));
  ASSERT(jacl_is_f32(jacl_f32(-1.0f)));
  ASSERT(jacl_is_f32(jacl_f32(FLT_MAX)));
  ASSERT(jacl_is_f32(jacl_f32(INFINITY)));
  ASSERT(jacl_is_f32(jacl_f32(NAN)));

  /* f32 with flags set is still f32 */
  ASSERT(jacl_is_f32(jacl_f32(1.0f) | JACL_FLAG_TAINTED));
  ASSERT(jacl_is_f32(jacl_f32(1.0f) | JACL_FLAG_SECRET));
  ASSERT(jacl_is_f32(jacl_f32(1.0f) | JACL_FLAG_ERROR));
  ASSERT(jacl_is_f32(jacl_f32(1.0f) | JACL_FLAGS_MASK));

  /* Other types are not f32 */
  ASSERT(!jacl_is_f32(JACL_NIL));
  ASSERT(!jacl_is_f32(JACL_TRUE));
  ASSERT(!jacl_is_f32(jacl_i32(42)));
  ASSERT(!jacl_is_f32(JACL_TAG_INLINE_STRING));

  TEST_PASS();
}

/* Helper: bitwise compare two floats via memcmp */
static int float_bitwise_eq(float a, float b) {
  return memcmp(&a, &b, sizeof(float)) == 0;
}

/* Test: jacl_as_f32 extractor and round-trip */
static int test_as_f32(void) {
  /* Basic round-trip values */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(0.0f)), 0.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(-0.0f)), -0.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(1.0f)), 1.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(-1.0f)), -1.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(FLT_MAX)), FLT_MAX));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(FLT_MIN)), FLT_MIN));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(FLT_EPSILON)), FLT_EPSILON));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(INFINITY)), INFINITY));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_f32(-INFINITY)), -INFINITY));

  TEST_PASS();
}

/* Test: NaN round-trips correctly (bitwise, not just isnan) */
static int test_f32_nan_roundtrip(void) {
  float nan_in = NAN;
  JaclVal v = jacl_f32(nan_in);
  float nan_out = jacl_as_f32(v);

  /* Must be NaN */
  ASSERT(isnan(nan_out));

  /* Must be bitwise identical */
  ASSERT(float_bitwise_eq(nan_out, nan_in));

  TEST_PASS();
}

/* Test: -0.0f and 0.0f are distinct bit patterns */
static int test_f32_neg_zero(void) {
  JaclVal vz = jacl_f32(0.0f);
  JaclVal vnz = jacl_f32(-0.0f);

  /* They should produce different JaclVal bit patterns */
  ASSERT(vz != vnz);

  /* Both round-trip correctly */
  ASSERT(float_bitwise_eq(jacl_as_f32(vz), 0.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(vnz), -0.0f));

  /* Confirm -0.0f and 0.0f are actually different bit patterns */
  ASSERT(!float_bitwise_eq(0.0f, -0.0f));

  TEST_PASS();
}

/* Test: f32 payload doesn't bleed into tag byte */
static int test_f32_payload_isolation(void) {
  /* FLT_MAX has a large bit pattern; verify no bleed */
  JaclVal v = jacl_f32(FLT_MAX);
  ASSERT_U64_EQ(v & JACL_TAG_MASK, JACL_TAG_F32);

  /* Negative infinity */
  JaclVal vinf = jacl_f32(-INFINITY);
  ASSERT_U64_EQ(vinf & JACL_TAG_MASK, JACL_TAG_F32);

  /* NaN */
  JaclVal vnan = jacl_f32(NAN);
  ASSERT_U64_EQ(vnan & JACL_TAG_MASK, JACL_TAG_F32);

  TEST_PASS();
}

/* ---- US-005 tests: Pointer-payload constructors and extractors ---- */

/* Test: pointer constructors produce correctly tagged values */
static int test_ptr_constructors(void) {
  int dummy;
  void *p = &dummy;

  ASSERT_U64_EQ(jacl_string_ptr(p) & JACL_TYPE_MASK, JACL_TAG_STRING);
  ASSERT_U64_EQ(jacl_vector_ptr(p) & JACL_TYPE_MASK, JACL_TAG_VECTOR);
  ASSERT_U64_EQ(jacl_map_ptr(p) & JACL_TYPE_MASK, JACL_TAG_MAP);
  ASSERT_U64_EQ(jacl_closure_ptr(p) & JACL_TYPE_MASK, JACL_TAG_CLOSURE);
  ASSERT_U64_EQ(jacl_bignum_ptr(p) & JACL_TYPE_MASK, JACL_TAG_BIGNUM);

  TEST_PASS();
}

/* Test: pointer type predicates (positive and negative) */
static int test_ptr_predicates(void) {
  int dummy;
  void *p = &dummy;

  JaclVal sv = jacl_string_ptr(p);
  JaclVal vv = jacl_vector_ptr(p);
  JaclVal mv = jacl_map_ptr(p);
  JaclVal cv = jacl_closure_ptr(p);
  JaclVal bv = jacl_bignum_ptr(p);

  /* Each predicate matches its own type */
  ASSERT(jacl_is_string(sv));
  ASSERT(jacl_is_vector(vv));
  ASSERT(jacl_is_map(mv));
  ASSERT(jacl_is_closure(cv));
  ASSERT(jacl_is_bignum(bv));

  /* Each predicate rejects other pointer types */
  ASSERT(!jacl_is_string(vv));
  ASSERT(!jacl_is_string(mv));
  ASSERT(!jacl_is_string(cv));
  ASSERT(!jacl_is_string(bv));

  ASSERT(!jacl_is_vector(sv));
  ASSERT(!jacl_is_vector(mv));
  ASSERT(!jacl_is_vector(cv));
  ASSERT(!jacl_is_vector(bv));

  ASSERT(!jacl_is_map(sv));
  ASSERT(!jacl_is_map(vv));
  ASSERT(!jacl_is_map(cv));
  ASSERT(!jacl_is_map(bv));

  ASSERT(!jacl_is_closure(sv));
  ASSERT(!jacl_is_closure(vv));
  ASSERT(!jacl_is_closure(mv));
  ASSERT(!jacl_is_closure(bv));

  ASSERT(!jacl_is_bignum(sv));
  ASSERT(!jacl_is_bignum(vv));
  ASSERT(!jacl_is_bignum(mv));
  ASSERT(!jacl_is_bignum(cv));

  /* Non-pointer types are not pointer types */
  ASSERT(!jacl_is_string(JACL_NIL));
  ASSERT(!jacl_is_string(JACL_TRUE));
  ASSERT(!jacl_is_string(jacl_i32(42)));
  ASSERT(!jacl_is_string(jacl_f32(1.0f)));

  ASSERT(!jacl_is_vector(JACL_NIL));
  ASSERT(!jacl_is_map(JACL_TRUE));
  ASSERT(!jacl_is_closure(jacl_i32(0)));
  ASSERT(!jacl_is_bignum(jacl_f32(0.0f)));

  TEST_PASS();
}

/* Test: pointer predicates ignore flag bits */
static int test_ptr_predicates_with_flags(void) {
  int dummy;
  void *p = &dummy;

  JaclVal sv = jacl_string_ptr(p);

  /* String with flags set is still string */
  ASSERT(jacl_is_string(sv | JACL_FLAG_TAINTED));
  ASSERT(jacl_is_string(sv | JACL_FLAG_SECRET));
  ASSERT(jacl_is_string(sv | JACL_FLAG_ERROR));
  ASSERT(jacl_is_string(sv | JACL_FLAGS_MASK));

  /* Same for other pointer types */
  ASSERT(jacl_is_vector(jacl_vector_ptr(p) | JACL_FLAGS_MASK));
  ASSERT(jacl_is_map(jacl_map_ptr(p) | JACL_FLAGS_MASK));
  ASSERT(jacl_is_closure(jacl_closure_ptr(p) | JACL_FLAGS_MASK));
  ASSERT(jacl_is_bignum(jacl_bignum_ptr(p) | JACL_FLAGS_MASK));

  TEST_PASS();
}

/* Test: pointer round-trip with malloc'd aligned objects */
static int test_ptr_roundtrip(void) {
  void *p1 = malloc(64);
  void *p2 = malloc(128);
  void *p3 = malloc(256);
  void *p4 = malloc(512);
  void *p5 = malloc(1024);

  ASSERT(p1 && p2 && p3 && p4 && p5);

  /* Round-trip: pointer survives pack/unpack unchanged */
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_string_ptr(p1)), p1);
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_vector_ptr(p2)), p2);
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_map_ptr(p3)), p3);
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_closure_ptr(p4)), p4);
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_bignum_ptr(p5)), p5);

  free(p1);
  free(p2);
  free(p3);
  free(p4);
  free(p5);

  TEST_PASS();
}

/* Test: NULL pointer round-trip */
static int test_ptr_null(void) {
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_string_ptr(NULL)), NULL);
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_vector_ptr(NULL)), NULL);
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_map_ptr(NULL)), NULL);
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_closure_ptr(NULL)), NULL);
  ASSERT_PTR_EQ(jacl_as_ptr(jacl_bignum_ptr(NULL)), NULL);

  TEST_PASS();
}

/* Test: pointer payload does not bleed into tag byte */
static int test_ptr_payload_isolation(void) {
  void *p = malloc(64);
  ASSERT(p != NULL);

  /* Tag byte should contain only the type tag, no pointer bits */
  ASSERT_U64_EQ(jacl_string_ptr(p) & JACL_TAG_MASK, JACL_TAG_STRING);
  ASSERT_U64_EQ(jacl_vector_ptr(p) & JACL_TAG_MASK, JACL_TAG_VECTOR);
  ASSERT_U64_EQ(jacl_map_ptr(p) & JACL_TAG_MASK, JACL_TAG_MAP);
  ASSERT_U64_EQ(jacl_closure_ptr(p) & JACL_TAG_MASK, JACL_TAG_CLOSURE);
  ASSERT_U64_EQ(jacl_bignum_ptr(p) & JACL_TAG_MASK, JACL_TAG_BIGNUM);

  free(p);

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

  /* US-004: f32 constructor, predicate, extractor */
  printf("Test: f32 constructor\n");
  if (!test_f32_constructor()) return 1;

  printf("Test: jacl_is_f32 predicate\n");
  if (!test_is_f32()) return 1;

  printf("Test: jacl_as_f32 extractor\n");
  if (!test_as_f32()) return 1;

  printf("Test: f32 NaN round-trip\n");
  if (!test_f32_nan_roundtrip()) return 1;

  printf("Test: f32 negative zero\n");
  if (!test_f32_neg_zero()) return 1;

  printf("Test: f32 payload isolation\n");
  if (!test_f32_payload_isolation()) return 1;

  /* US-005: Pointer-payload constructors and extractors */
  printf("Test: pointer constructors\n");
  if (!test_ptr_constructors()) return 1;

  printf("Test: pointer predicates\n");
  if (!test_ptr_predicates()) return 1;

  printf("Test: pointer predicates with flags\n");
  if (!test_ptr_predicates_with_flags()) return 1;

  printf("Test: pointer round-trip\n");
  if (!test_ptr_roundtrip()) return 1;

  printf("Test: NULL pointer round-trip\n");
  if (!test_ptr_null()) return 1;

  printf("Test: pointer payload isolation\n");
  if (!test_ptr_payload_isolation()) return 1;

  return 0;
}
