#include <float.h>
#include <limits.h>
#include <math.h>

#include "test_helpers.h"
#include "../src/jacl.h"

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

/* ---- US-006 tests: Inline string packing and unpacking ---- */

/* Test: empty inline string (len 0) */
static int test_inline_string_empty(void) {
  JaclVal v = jacl_inline_string("", 0);
  char buf[8];

  ASSERT(jacl_is_inline_string(v));
  ASSERT_SIZE_EQ(jacl_inline_string_len(v), 0);

  jacl_inline_string_get(v, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "");

  /* Payload should be zero for empty string */
  ASSERT_U64_EQ(v & JACL_PAYLOAD_MASK, 0);

  TEST_PASS();
}

/* Test: single char inline string */
static int test_inline_string_single_char(void) {
  JaclVal v = jacl_inline_string("A", 1);
  char buf[8];

  ASSERT(jacl_is_inline_string(v));
  ASSERT_SIZE_EQ(jacl_inline_string_len(v), 1);

  jacl_inline_string_get(v, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "A");

  TEST_PASS();
}

/* Test: 6-byte inline string */
static int test_inline_string_6_bytes(void) {
  JaclVal v = jacl_inline_string("abcdef", 6);
  char buf[8];

  ASSERT(jacl_is_inline_string(v));
  ASSERT_SIZE_EQ(jacl_inline_string_len(v), 6);

  jacl_inline_string_get(v, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "abcdef");

  TEST_PASS();
}

/* Test: 7-byte inline string (maximum) */
static int test_inline_string_7_bytes(void) {
  JaclVal v = jacl_inline_string("abcdefg", 7);
  char buf[8];

  ASSERT(jacl_is_inline_string(v));
  ASSERT_SIZE_EQ(jacl_inline_string_len(v), 7);

  jacl_inline_string_get(v, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "abcdefg");

  TEST_PASS();
}

/* Test: round-trip for all valid lengths */
static int test_inline_string_roundtrip(void) {
  const char *strs[] = {"", "x", "ab", "abc", "abcd", "abcde", "abcdef", "abcdefg"};
  size_t lens[] = {0, 1, 2, 3, 4, 5, 6, 7};
  char buf[8];

  for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
    JaclVal v = jacl_inline_string(strs[i], lens[i]);
    ASSERT_SIZE_EQ(jacl_inline_string_len(v), lens[i]);
    jacl_inline_string_get(v, buf, sizeof(buf));
    ASSERT(memcmp(buf, strs[i], lens[i]) == 0);
    ASSERT(buf[lens[i]] == '\0');
  }

  TEST_PASS();
}

/* Test: strings with various non-null byte values */
static int test_inline_string_byte_values(void) {
  /* High-bit bytes */
  char s1[] = {(char)0xFF, (char)0x80, (char)0x7F, 0};
  JaclVal v1 = jacl_inline_string(s1, 3);
  char buf[8];

  ASSERT_SIZE_EQ(jacl_inline_string_len(v1), 3);
  jacl_inline_string_get(v1, buf, sizeof(buf));
  ASSERT((unsigned char)buf[0] == 0xFF);
  ASSERT((unsigned char)buf[1] == 0x80);
  ASSERT((unsigned char)buf[2] == 0x7F);
  ASSERT(buf[3] == '\0');

  /* All bytes = 0x01 (near-zero but non-null) */
  char s2[] = {1, 1, 1, 1, 1, 1, 1};
  JaclVal v2 = jacl_inline_string(s2, 7);
  ASSERT_SIZE_EQ(jacl_inline_string_len(v2), 7);
  jacl_inline_string_get(v2, buf, sizeof(buf));
  for (size_t i = 0; i < 7; i++) {
    ASSERT(buf[i] == 1);
  }
  ASSERT(buf[7] == '\0');

  TEST_PASS();
}

/* Test: INLINE_STRING type tag is distinct from STRING (heap pointer) type tag */
static int test_inline_string_vs_string_tag(void) {
  ASSERT(JACL_TAG_INLINE_STRING != JACL_TAG_STRING);

  int dummy;
  JaclVal istr = jacl_inline_string("test", 4);
  JaclVal hstr = jacl_string_ptr(&dummy);

  /* Inline string: is_inline_string and is_string both true, not heap */
  ASSERT(jacl_is_inline_string(istr));
  ASSERT(jacl_is_string(istr));
  ASSERT(!jacl_is_heap_string(istr));

  /* Heap string: is_heap_string and is_string both true, not inline */
  ASSERT(jacl_is_heap_string(hstr));
  ASSERT(jacl_is_string(hstr));
  ASSERT(!jacl_is_inline_string(hstr));

  TEST_PASS();
}

/* Test: inline string predicate ignores flag bits */
static int test_inline_string_predicate_with_flags(void) {
  JaclVal v = jacl_inline_string("hi", 2);

  ASSERT(jacl_is_inline_string(v | JACL_FLAG_TAINTED));
  ASSERT(jacl_is_inline_string(v | JACL_FLAG_SECRET));
  ASSERT(jacl_is_inline_string(v | JACL_FLAG_ERROR));
  ASSERT(jacl_is_inline_string(v | JACL_FLAGS_MASK));

  /* Other types are not inline string */
  ASSERT(!jacl_is_inline_string(JACL_NIL));
  ASSERT(!jacl_is_inline_string(JACL_TRUE));
  ASSERT(!jacl_is_inline_string(jacl_i32(42)));
  ASSERT(!jacl_is_inline_string(jacl_f32(1.0f)));

  TEST_PASS();
}

/* Test: null in input string acts as terminator */
static int test_inline_string_null_terminates(void) {
  /* String "ab\0cd" with len 5 should only store "ab" */
  char s[] = {'a', 'b', '\0', 'c', 'd'};
  JaclVal v = jacl_inline_string(s, 5);
  char buf[8];

  ASSERT_SIZE_EQ(jacl_inline_string_len(v), 2);
  jacl_inline_string_get(v, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "ab");

  TEST_PASS();
}

/* ---- US-007 tests: Tainted/secret/error flag manipulation ---- */

/* Test: set/get/clear tainted flag */
static int test_flag_tainted(void) {
  JaclVal v = jacl_i32(42);
  ASSERT(!jacl_is_tainted(v));

  JaclVal t = jacl_set_tainted(v);
  ASSERT(jacl_is_tainted(t));

  JaclVal c = jacl_clear_tainted(t);
  ASSERT(!jacl_is_tainted(c));

  /* Value semantics: original unchanged */
  ASSERT(!jacl_is_tainted(v));

  TEST_PASS();
}

/* Test: set/get/clear secret flag */
static int test_flag_secret(void) {
  JaclVal v = jacl_i32(42);
  ASSERT(!jacl_is_secret(v));

  JaclVal s = jacl_set_secret(v);
  ASSERT(jacl_is_secret(s));

  JaclVal c = jacl_clear_secret(s);
  ASSERT(!jacl_is_secret(c));

  TEST_PASS();
}

/* Test: set/get/clear error flag */
static int test_flag_error(void) {
  JaclVal v = jacl_i32(42);
  ASSERT(!jacl_is_error(v));

  JaclVal e = jacl_set_error(v);
  ASSERT(jacl_is_error(e));

  JaclVal c = jacl_clear_error(e);
  ASSERT(!jacl_is_error(c));

  TEST_PASS();
}

/* Test: setting a flag preserves type tag and payload */
static int test_flag_preserves_type_payload(void) {
  JaclVal v = jacl_i32(-7);

  /* Set each flag and verify type + payload preserved */
  JaclVal t = jacl_set_tainted(v);
  ASSERT(jacl_is_i32(t));
  ASSERT_INT_EQ(jacl_as_i32(t), -7);

  JaclVal s = jacl_set_secret(v);
  ASSERT(jacl_is_i32(s));
  ASSERT_INT_EQ(jacl_as_i32(s), -7);

  JaclVal e = jacl_set_error(v);
  ASSERT(jacl_is_i32(e));
  ASSERT_INT_EQ(jacl_as_i32(e), -7);

  /* All three flags at once */
  JaclVal all = jacl_set_tainted(jacl_set_secret(jacl_set_error(v)));
  ASSERT(jacl_is_i32(all));
  ASSERT_INT_EQ(jacl_as_i32(all), -7);

  TEST_PASS();
}

/* Test: clearing a flag preserves type tag and payload */
static int test_clear_preserves_type_payload(void) {
  JaclVal v = jacl_set_tainted(jacl_set_secret(jacl_set_error(jacl_f32(3.14f))));

  JaclVal c1 = jacl_clear_tainted(v);
  ASSERT(jacl_is_f32(c1));
  ASSERT(float_bitwise_eq(jacl_as_f32(c1), 3.14f));
  ASSERT(!jacl_is_tainted(c1));
  ASSERT(jacl_is_secret(c1));
  ASSERT(jacl_is_error(c1));

  JaclVal c2 = jacl_clear_secret(v);
  ASSERT(jacl_is_f32(c2));
  ASSERT(float_bitwise_eq(jacl_as_f32(c2), 3.14f));
  ASSERT(jacl_is_tainted(c2));
  ASSERT(!jacl_is_secret(c2));
  ASSERT(jacl_is_error(c2));

  JaclVal c3 = jacl_clear_error(v);
  ASSERT(jacl_is_f32(c3));
  ASSERT(float_bitwise_eq(jacl_as_f32(c3), 3.14f));
  ASSERT(jacl_is_tainted(c3));
  ASSERT(jacl_is_secret(c3));
  ASSERT(!jacl_is_error(c3));

  TEST_PASS();
}

/* Test: multiple flags can be set simultaneously */
static int test_flag_multiple(void) {
  JaclVal v = jacl_bool(true);

  /* Set tainted + error */
  JaclVal te = jacl_set_tainted(jacl_set_error(v));
  ASSERT(jacl_is_tainted(te));
  ASSERT(!jacl_is_secret(te));
  ASSERT(jacl_is_error(te));

  /* Set all three */
  JaclVal all = jacl_set_secret(te);
  ASSERT(jacl_is_tainted(all));
  ASSERT(jacl_is_secret(all));
  ASSERT(jacl_is_error(all));

  /* Still a bool with correct payload */
  ASSERT(jacl_is_bool(all));
  ASSERT(jacl_as_bool(all) == true);

  TEST_PASS();
}

/* Test: flag independence (setting one doesn't affect others) */
static int test_flag_independence(void) {
  JaclVal v = JACL_NIL;

  /* Set only tainted */
  JaclVal t = jacl_set_tainted(v);
  ASSERT(jacl_is_tainted(t));
  ASSERT(!jacl_is_secret(t));
  ASSERT(!jacl_is_error(t));

  /* Set only secret */
  JaclVal s = jacl_set_secret(v);
  ASSERT(!jacl_is_tainted(s));
  ASSERT(jacl_is_secret(s));
  ASSERT(!jacl_is_error(s));

  /* Set only error */
  JaclVal e = jacl_set_error(v);
  ASSERT(!jacl_is_tainted(e));
  ASSERT(!jacl_is_secret(e));
  ASSERT(jacl_is_error(e));

  /* Clear one from all: only that one clears */
  JaclVal all = jacl_set_tainted(jacl_set_secret(jacl_set_error(v)));
  JaclVal ct = jacl_clear_tainted(all);
  ASSERT(!jacl_is_tainted(ct));
  ASSERT(jacl_is_secret(ct));
  ASSERT(jacl_is_error(ct));

  JaclVal cs = jacl_clear_secret(all);
  ASSERT(jacl_is_tainted(cs));
  ASSERT(!jacl_is_secret(cs));
  ASSERT(jacl_is_error(cs));

  JaclVal ce = jacl_clear_error(all);
  ASSERT(jacl_is_tainted(ce));
  ASSERT(jacl_is_secret(ce));
  ASSERT(!jacl_is_error(ce));

  TEST_PASS();
}

/* Test: flags work correctly with all value types */
static int test_flags_all_types(void) {
  int dummy;
  JaclVal values[] = {
    JACL_NIL,
    JACL_TRUE,
    JACL_FALSE,
    jacl_i32(123),
    jacl_f32(2.5f),
    jacl_string_ptr(&dummy),
    jacl_inline_string("hello", 5)
  };
  size_t n = sizeof(values) / sizeof(values[0]);

  for (size_t i = 0; i < n; i++) {
    JaclVal v = values[i];
    uint64_t type_before = v & JACL_TYPE_MASK;
    uint64_t payload_before = v & JACL_PAYLOAD_MASK;

    /* Set each flag and verify type+payload preserved */
    JaclVal t = jacl_set_tainted(v);
    ASSERT(jacl_is_tainted(t));
    ASSERT_U64_EQ(t & JACL_TYPE_MASK, type_before);
    ASSERT_U64_EQ(t & JACL_PAYLOAD_MASK, payload_before);

    JaclVal s = jacl_set_secret(v);
    ASSERT(jacl_is_secret(s));
    ASSERT_U64_EQ(s & JACL_TYPE_MASK, type_before);
    ASSERT_U64_EQ(s & JACL_PAYLOAD_MASK, payload_before);

    JaclVal e = jacl_set_error(v);
    ASSERT(jacl_is_error(e));
    ASSERT_U64_EQ(e & JACL_TYPE_MASK, type_before);
    ASSERT_U64_EQ(e & JACL_PAYLOAD_MASK, payload_before);

    /* Set all, clear all, should get back to original */
    JaclVal all = jacl_set_tainted(jacl_set_secret(jacl_set_error(v)));
    JaclVal cleared = jacl_clear_tainted(jacl_clear_secret(jacl_clear_error(all)));
    ASSERT_U64_EQ(cleared, v);
  }

  TEST_PASS();
}

/* Test: setting an already-set flag is idempotent */
static int test_flag_idempotent(void) {
  JaclVal v = jacl_set_tainted(jacl_i32(99));
  JaclVal v2 = jacl_set_tainted(v);
  ASSERT_U64_EQ(v, v2);

  JaclVal c = jacl_clear_tainted(jacl_i32(99));
  JaclVal c2 = jacl_clear_tainted(c);
  ASSERT_U64_EQ(c, c2);

  TEST_PASS();
}

/* ---- US-008 tests: Flag propagation helpers ---- */

/* Test: propagate_flags returns OR of both operands' flags */
static int test_propagate_flags_basic(void) {
  JaclVal a = jacl_set_tainted(jacl_i32(1));
  JaclVal b = jacl_set_secret(jacl_i32(2));

  uint64_t flags = jacl_propagate_flags(a, b);
  ASSERT(flags & JACL_FLAG_TAINTED);
  ASSERT(flags & JACL_FLAG_SECRET);
  ASSERT(!(flags & JACL_FLAG_ERROR));

  /* Only flag bits should be set */
  ASSERT_U64_EQ(flags & ~JACL_FLAGS_MASK, 0);

  TEST_PASS();
}

/* Test: propagate_flags with no flags on either operand */
static int test_propagate_flags_none(void) {
  JaclVal a = jacl_i32(10);
  JaclVal b = jacl_f32(2.0f);

  uint64_t flags = jacl_propagate_flags(a, b);
  ASSERT_U64_EQ(flags, 0);

  TEST_PASS();
}

/* Test: propagate_flags with one flag each (distinct) */
static int test_propagate_flags_one_each(void) {
  /* tainted + error */
  JaclVal a = jacl_set_tainted(jacl_i32(1));
  JaclVal b = jacl_set_error(jacl_i32(2));

  uint64_t flags = jacl_propagate_flags(a, b);
  ASSERT(flags & JACL_FLAG_TAINTED);
  ASSERT(!(flags & JACL_FLAG_SECRET));
  ASSERT(flags & JACL_FLAG_ERROR);

  /* secret + error */
  JaclVal c = jacl_set_secret(jacl_i32(3));
  JaclVal d = jacl_set_error(jacl_i32(4));

  uint64_t flags2 = jacl_propagate_flags(c, d);
  ASSERT(!(flags2 & JACL_FLAG_TAINTED));
  ASSERT(flags2 & JACL_FLAG_SECRET);
  ASSERT(flags2 & JACL_FLAG_ERROR);

  TEST_PASS();
}

/* Test: propagate_flags with all flags on both operands */
static int test_propagate_flags_all(void) {
  JaclVal a = jacl_set_tainted(jacl_set_secret(jacl_set_error(jacl_i32(1))));
  JaclVal b = jacl_set_tainted(jacl_set_secret(jacl_set_error(jacl_i32(2))));

  uint64_t flags = jacl_propagate_flags(a, b);
  ASSERT(flags & JACL_FLAG_TAINTED);
  ASSERT(flags & JACL_FLAG_SECRET);
  ASSERT(flags & JACL_FLAG_ERROR);
  ASSERT_U64_EQ(flags, JACL_FLAGS_MASK);

  TEST_PASS();
}

/* Test: propagate_flags with mixed flags */
static int test_propagate_flags_mixed(void) {
  /* a has tainted+secret, b has secret+error -> result has all three */
  JaclVal a = jacl_set_tainted(jacl_set_secret(jacl_i32(1)));
  JaclVal b = jacl_set_secret(jacl_set_error(jacl_i32(2)));

  uint64_t flags = jacl_propagate_flags(a, b);
  ASSERT(flags & JACL_FLAG_TAINTED);
  ASSERT(flags & JACL_FLAG_SECRET);
  ASSERT(flags & JACL_FLAG_ERROR);

  TEST_PASS();
}

/* Test: propagate_flags — error on either operand propagates */
static int test_propagate_flags_error_either(void) {
  JaclVal clean = jacl_i32(1);
  JaclVal errored = jacl_set_error(jacl_i32(2));

  /* Error on b */
  uint64_t f1 = jacl_propagate_flags(clean, errored);
  ASSERT(f1 & JACL_FLAG_ERROR);

  /* Error on a */
  uint64_t f2 = jacl_propagate_flags(errored, clean);
  ASSERT(f2 & JACL_FLAG_ERROR);

  TEST_PASS();
}

/* Test: apply_flags applies flag bits onto a result */
static int test_apply_flags_basic(void) {
  JaclVal result = jacl_i32(42);
  uint64_t flags = JACL_FLAG_TAINTED | JACL_FLAG_SECRET;

  JaclVal flagged = jacl_apply_flags(result, flags);

  /* Result should have the flags */
  ASSERT(jacl_is_tainted(flagged));
  ASSERT(jacl_is_secret(flagged));
  ASSERT(!jacl_is_error(flagged));

  /* Type tag and payload should be preserved */
  ASSERT(jacl_is_i32(flagged));
  ASSERT_INT_EQ(jacl_as_i32(flagged), 42);

  TEST_PASS();
}

/* Test: apply_flags with zero flags is a no-op */
static int test_apply_flags_zero(void) {
  JaclVal result = jacl_f32(3.14f);
  JaclVal flagged = jacl_apply_flags(result, 0);
  ASSERT_U64_EQ(flagged, result);

  TEST_PASS();
}

/* Test: apply_flags preserves existing flags on result */
static int test_apply_flags_preserves_existing(void) {
  JaclVal result = jacl_set_tainted(jacl_i32(7));
  uint64_t flags = JACL_FLAG_ERROR;

  JaclVal flagged = jacl_apply_flags(result, flags);
  ASSERT(jacl_is_tainted(flagged));  /* already on result */
  ASSERT(jacl_is_error(flagged));     /* newly applied */
  ASSERT(!jacl_is_secret(flagged));

  ASSERT(jacl_is_i32(flagged));
  ASSERT_INT_EQ(jacl_as_i32(flagged), 7);

  TEST_PASS();
}

/* Test: apply_flags only applies flag bits (ignores non-flag bits in input) */
static int test_apply_flags_masks_input(void) {
  JaclVal result = jacl_i32(10);
  /* Pass garbage bits plus a real flag */
  uint64_t flags = JACL_FLAG_SECRET | UINT64_C(0x00FFFFFFFFFFFFFF);

  JaclVal flagged = jacl_apply_flags(result, flags);
  /* Only the secret flag should be applied */
  ASSERT(jacl_is_secret(flagged));
  /* Type and payload preserved */
  ASSERT(jacl_is_i32(flagged));
  ASSERT_INT_EQ(jacl_as_i32(flagged), 10);

  TEST_PASS();
}

/* Test: end-to-end propagate then apply */
static int test_propagate_and_apply(void) {
  JaclVal a = jacl_set_tainted(jacl_i32(3));
  JaclVal b = jacl_set_error(jacl_i32(4));

  uint64_t flags = jacl_propagate_flags(a, b);
  JaclVal result = jacl_apply_flags(jacl_i32(7), flags);

  ASSERT(jacl_is_tainted(result));
  ASSERT(!jacl_is_secret(result));
  ASSERT(jacl_is_error(result));
  ASSERT(jacl_is_i32(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 7);

  TEST_PASS();
}

/* Test: propagate_flags with various flag combinations (spot-check 2^3 = 8 per operand) */
static int test_propagate_flags_combinations(void) {
  /* All 8 possible flag combos */
  uint64_t combos[8] = {
    0,
    JACL_FLAG_TAINTED,
    JACL_FLAG_SECRET,
    JACL_FLAG_ERROR,
    JACL_FLAG_TAINTED | JACL_FLAG_SECRET,
    JACL_FLAG_TAINTED | JACL_FLAG_ERROR,
    JACL_FLAG_SECRET | JACL_FLAG_ERROR,
    JACL_FLAG_TAINTED | JACL_FLAG_SECRET | JACL_FLAG_ERROR
  };

  /* Spot-check: for each combo as a, pair with selected b combos */
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      JaclVal a = jacl_i32(1) | combos[i];
      JaclVal b = jacl_i32(2) | combos[j];

      uint64_t flags = jacl_propagate_flags(a, b);
      uint64_t expected = combos[i] | combos[j];
      ASSERT_U64_EQ(flags, expected);
    }
  }

  TEST_PASS();
}

/* ---- US-009 tests: Complete type predicate set and type_tag extractor ---- */

/* Test: jacl_type_tag extracts the 5-bit type tag, ignoring flags */
static int test_type_tag_extractor(void) {
  ASSERT_U64_EQ(jacl_type_tag(JACL_NIL), JACL_TAG_NIL);
  ASSERT_U64_EQ(jacl_type_tag(JACL_TRUE), JACL_TAG_BOOL);
  ASSERT_U64_EQ(jacl_type_tag(JACL_FALSE), JACL_TAG_BOOL);
  ASSERT_U64_EQ(jacl_type_tag(jacl_i32(42)), JACL_TAG_I32);
  ASSERT_U64_EQ(jacl_type_tag(jacl_f32(1.0f)), JACL_TAG_F32);
  ASSERT_U64_EQ(jacl_type_tag(jacl_inline_string("hi", 2)), JACL_TAG_INLINE_STRING);

  int dummy;
  ASSERT_U64_EQ(jacl_type_tag(jacl_string_ptr(&dummy)), JACL_TAG_STRING);
  ASSERT_U64_EQ(jacl_type_tag(jacl_vector_ptr(&dummy)), JACL_TAG_VECTOR);
  ASSERT_U64_EQ(jacl_type_tag(jacl_map_ptr(&dummy)), JACL_TAG_MAP);
  ASSERT_U64_EQ(jacl_type_tag(jacl_closure_ptr(&dummy)), JACL_TAG_CLOSURE);
  ASSERT_U64_EQ(jacl_type_tag(jacl_bignum_ptr(&dummy)), JACL_TAG_BIGNUM);

  /* Flags should be masked out */
  ASSERT_U64_EQ(jacl_type_tag(jacl_set_tainted(jacl_i32(1))), JACL_TAG_I32);
  ASSERT_U64_EQ(jacl_type_tag(jacl_set_secret(jacl_f32(1.0f))), JACL_TAG_F32);
  ASSERT_U64_EQ(jacl_type_tag(jacl_set_error(JACL_NIL)), JACL_TAG_NIL);
  ASSERT_U64_EQ(jacl_type_tag(jacl_set_tainted(jacl_set_secret(jacl_set_error(JACL_TRUE)))), JACL_TAG_BOOL);

  TEST_PASS();
}

/* Test: NxN predicate matrix — each predicate returns true only for its own type */
static int test_predicate_matrix(void) {
  int dummy;
  /* One value of each type */
  JaclVal values[] = {
    JACL_NIL,                          /* 0: nil */
    JACL_TRUE,                         /* 1: bool */
    jacl_i32(42),                      /* 2: i32 */
    jacl_f32(3.14f),                   /* 3: f32 */
    jacl_inline_string("test", 4),     /* 4: inline_string */
    jacl_string_ptr(&dummy),           /* 5: string (ptr) */
    jacl_vector_ptr(&dummy),           /* 6: vector */
    jacl_map_ptr(&dummy),              /* 7: map */
    jacl_closure_ptr(&dummy),          /* 8: closure */
    jacl_bignum_ptr(&dummy)            /* 9: bignum */
  };
  size_t n = sizeof(values) / sizeof(values[0]);

  /* Expected type tags in same order */
  uint64_t expected_tags[] = {
    JACL_TAG_NIL, JACL_TAG_BOOL, JACL_TAG_I32, JACL_TAG_F32,
    JACL_TAG_INLINE_STRING, JACL_TAG_STRING, JACL_TAG_VECTOR,
    JACL_TAG_MAP, JACL_TAG_CLOSURE, JACL_TAG_BIGNUM
  };

  /* Predicate function pointers */
  typedef bool (*pred_fn)(JaclVal);
  pred_fn preds[] = {
    jacl_is_nil, jacl_is_bool, jacl_is_i32, jacl_is_f32,
    jacl_is_inline_string, jacl_is_heap_string, jacl_is_vector,
    jacl_is_map, jacl_is_closure, jacl_is_bignum
  };

  const char *names[] = {
    "nil", "bool", "i32", "f32", "inline_string",
    "heap_string", "vector", "map", "closure", "bignum"
  };

  /* Verify type tags */
  for (size_t i = 0; i < n; i++) {
    ASSERT_U64_EQ(jacl_type_tag(values[i]), expected_tags[i]);
  }

  /* NxN: each predicate should return true only for its own type */
  for (size_t pred_idx = 0; pred_idx < n; pred_idx++) {
    for (size_t val_idx = 0; val_idx < n; val_idx++) {
      bool result = preds[pred_idx](values[val_idx]);
      bool expected = (pred_idx == val_idx);
      if (result != expected) {
        fprintf(stderr, "FAIL: jacl_is_%s(value[%zu]=%s) = %d, expected %d\n",
                names[pred_idx], val_idx, names[val_idx], result, expected);
        return 0;
      }
    }
  }

  TEST_PASS();
}

/* Test: predicates ignore flags — error-flagged values still match their type */
static int test_predicate_matrix_with_flags(void) {
  int dummy;
  JaclVal values[] = {
    JACL_NIL,
    JACL_TRUE,
    jacl_i32(42),
    jacl_f32(3.14f),
    jacl_inline_string("test", 4),
    jacl_string_ptr(&dummy),
    jacl_vector_ptr(&dummy),
    jacl_map_ptr(&dummy),
    jacl_closure_ptr(&dummy),
    jacl_bignum_ptr(&dummy)
  };
  size_t n = sizeof(values) / sizeof(values[0]);

  typedef bool (*pred_fn)(JaclVal);
  pred_fn preds[] = {
    jacl_is_nil, jacl_is_bool, jacl_is_i32, jacl_is_f32,
    jacl_is_inline_string, jacl_is_heap_string, jacl_is_vector,
    jacl_is_map, jacl_is_closure, jacl_is_bignum
  };

  /* Flag each value with all flags and re-check the matrix */
  for (size_t val_idx = 0; val_idx < n; val_idx++) {
    JaclVal flagged = values[val_idx] | JACL_FLAGS_MASK;

    /* type_tag should still match */
    ASSERT_U64_EQ(jacl_type_tag(flagged), jacl_type_tag(values[val_idx]));

    for (size_t pred_idx = 0; pred_idx < n; pred_idx++) {
      bool result = preds[pred_idx](flagged);
      bool expected = (pred_idx == val_idx);
      if (result != expected) {
        fprintf(stderr, "FAIL: jacl_is_[%zu](flagged_value[%zu]) = %d, expected %d\n",
                pred_idx, val_idx, result, expected);
        return 0;
      }
    }
  }

  TEST_PASS();
}

/* ---- US-010 tests: i32 arithmetic operations with flag propagation ---- */

/* Test: basic i32 arithmetic */
static int test_i32_arithmetic_basic(void) {
  /* 3 + 4 = 7 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_add_i32(jacl_i32(3), jacl_i32(4))), 7);

  /* 10 - 3 = 7 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_sub_i32(jacl_i32(10), jacl_i32(3))), 7);

  /* 6 * 7 = 42 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_mul_i32(jacl_i32(6), jacl_i32(7))), 42);

  /* 10 / 3 = 3 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_div_i32(jacl_i32(10), jacl_i32(3))), 3);

  /* 10 % 3 = 1 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_mod_i32(jacl_i32(10), jacl_i32(3))), 1);

  /* All results should be i32-tagged */
  ASSERT(jacl_is_i32(jacl_add_i32(jacl_i32(1), jacl_i32(2))));
  ASSERT(jacl_is_i32(jacl_sub_i32(jacl_i32(5), jacl_i32(3))));
  ASSERT(jacl_is_i32(jacl_mul_i32(jacl_i32(2), jacl_i32(3))));
  ASSERT(jacl_is_i32(jacl_div_i32(jacl_i32(6), jacl_i32(2))));
  ASSERT(jacl_is_i32(jacl_mod_i32(jacl_i32(7), jacl_i32(4))));

  TEST_PASS();
}

/* Test: i32 arithmetic with negative numbers */
static int test_i32_arithmetic_negative(void) {
  /* -3 + 4 = 1 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_add_i32(jacl_i32(-3), jacl_i32(4))), 1);

  /* 3 + (-4) = -1 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_add_i32(jacl_i32(3), jacl_i32(-4))), -1);

  /* -3 - (-4) = 1 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_sub_i32(jacl_i32(-3), jacl_i32(-4))), 1);

  /* -6 * 7 = -42 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_mul_i32(jacl_i32(-6), jacl_i32(7))), -42);

  /* -6 * -7 = 42 */
  ASSERT_INT_EQ(jacl_as_i32(jacl_mul_i32(jacl_i32(-6), jacl_i32(-7))), 42);

  /* -10 / 3 = -3 (truncation toward zero in C99) */
  ASSERT_INT_EQ(jacl_as_i32(jacl_div_i32(jacl_i32(-10), jacl_i32(3))), -3);

  /* -10 % 3 = -1 (sign follows dividend in C99) */
  ASSERT_INT_EQ(jacl_as_i32(jacl_mod_i32(jacl_i32(-10), jacl_i32(3))), -1);

  TEST_PASS();
}

/* Test: jacl_neg_i32 */
static int test_i32_neg(void) {
  ASSERT_INT_EQ(jacl_as_i32(jacl_neg_i32(jacl_i32(42))), -42);
  ASSERT_INT_EQ(jacl_as_i32(jacl_neg_i32(jacl_i32(-42))), 42);
  ASSERT_INT_EQ(jacl_as_i32(jacl_neg_i32(jacl_i32(0))), 0);
  ASSERT_INT_EQ(jacl_as_i32(jacl_neg_i32(jacl_i32(1))), -1);
  ASSERT_INT_EQ(jacl_as_i32(jacl_neg_i32(jacl_i32(-1))), 1);

  /* Result should be i32-tagged */
  ASSERT(jacl_is_i32(jacl_neg_i32(jacl_i32(42))));

  TEST_PASS();
}

/* Test: division by zero returns error-flagged value */
static int test_i32_div_by_zero(void) {
  JaclVal result = jacl_div_i32(jacl_i32(10), jacl_i32(0));
  ASSERT(jacl_is_error(result));
  ASSERT(jacl_is_i32(result));

  TEST_PASS();
}

/* Test: mod by zero returns error-flagged value */
static int test_i32_mod_by_zero(void) {
  JaclVal result = jacl_mod_i32(jacl_i32(10), jacl_i32(0));
  ASSERT(jacl_is_error(result));
  ASSERT(jacl_is_i32(result));

  TEST_PASS();
}

/* Test: error operand short-circuits (returns the error operand) */
static int test_i32_error_shortcircuit(void) {
  JaclVal err_a = jacl_set_error(jacl_i32(99));
  JaclVal ok_b = jacl_i32(5);

  /* Error on a: returns a */
  ASSERT_U64_EQ(jacl_add_i32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_sub_i32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_mul_i32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_div_i32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_mod_i32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_neg_i32(err_a), err_a);

  /* Error on b: returns b */
  JaclVal err_b = jacl_set_error(jacl_i32(77));
  ASSERT_U64_EQ(jacl_add_i32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_sub_i32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_mul_i32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_div_i32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_mod_i32(ok_b, err_b), err_b);

  /* Both error: returns a (first checked) */
  ASSERT_U64_EQ(jacl_add_i32(err_a, err_b), err_a);

  TEST_PASS();
}

/* Test: flag propagation through arithmetic */
static int test_i32_flag_propagation(void) {
  JaclVal tainted_a = jacl_set_tainted(jacl_i32(3));
  JaclVal secret_b = jacl_set_secret(jacl_i32(4));

  /* add: tainted + secret -> result has both flags */
  JaclVal sum = jacl_add_i32(tainted_a, secret_b);
  ASSERT_INT_EQ(jacl_as_i32(sum), 7);
  ASSERT(jacl_is_tainted(sum));
  ASSERT(jacl_is_secret(sum));
  ASSERT(!jacl_is_error(sum));

  /* sub: same flags propagate */
  JaclVal diff = jacl_sub_i32(tainted_a, secret_b);
  ASSERT_INT_EQ(jacl_as_i32(diff), -1);
  ASSERT(jacl_is_tainted(diff));
  ASSERT(jacl_is_secret(diff));

  /* mul: clean * tainted -> tainted */
  JaclVal prod = jacl_mul_i32(jacl_i32(2), tainted_a);
  ASSERT_INT_EQ(jacl_as_i32(prod), 6);
  ASSERT(jacl_is_tainted(prod));
  ASSERT(!jacl_is_secret(prod));

  /* div: secret / clean -> secret */
  JaclVal quot = jacl_div_i32(secret_b, jacl_i32(2));
  ASSERT_INT_EQ(jacl_as_i32(quot), 2);
  ASSERT(jacl_is_secret(quot));
  ASSERT(!jacl_is_tainted(quot));

  /* mod: clean % clean -> no flags */
  JaclVal rem = jacl_mod_i32(jacl_i32(10), jacl_i32(3));
  ASSERT(!jacl_is_tainted(rem));
  ASSERT(!jacl_is_secret(rem));
  ASSERT(!jacl_is_error(rem));

  TEST_PASS();
}

/* Test: neg preserves flags */
static int test_i32_neg_preserves_flags(void) {
  JaclVal v = jacl_set_tainted(jacl_set_secret(jacl_i32(42)));
  JaclVal neg = jacl_neg_i32(v);

  ASSERT_INT_EQ(jacl_as_i32(neg), -42);
  ASSERT(jacl_is_tainted(neg));
  ASSERT(jacl_is_secret(neg));
  ASSERT(!jacl_is_error(neg));

  TEST_PASS();
}

/* Test: div/mod by zero propagates flags from operands */
static int test_i32_divmod_zero_flag_propagation(void) {
  JaclVal tainted_a = jacl_set_tainted(jacl_i32(10));
  JaclVal secret_zero = jacl_set_secret(jacl_i32(0));

  /* div by zero: result should have error + tainted + secret */
  JaclVal div_result = jacl_div_i32(tainted_a, secret_zero);
  ASSERT(jacl_is_error(div_result));
  ASSERT(jacl_is_tainted(div_result));
  ASSERT(jacl_is_secret(div_result));

  /* mod by zero: same */
  JaclVal mod_result = jacl_mod_i32(tainted_a, secret_zero);
  ASSERT(jacl_is_error(mod_result));
  ASSERT(jacl_is_tainted(mod_result));
  ASSERT(jacl_is_secret(mod_result));

  TEST_PASS();
}

/* Test: INT32_MIN edge cases */
static int test_i32_int32_min_edges(void) {
  /* -INT32_MIN overflows: in two's complement, -(-2147483648) wraps to -2147483648 */
  JaclVal neg_min = jacl_neg_i32(jacl_i32(INT32_MIN));
  /* C wraps on signed overflow (implementation-defined in C99, but we use uint32_t intermediates) */
  /* -INT32_MIN as int32_t is INT32_MIN due to two's complement wrap */
  ASSERT_INT_EQ(jacl_as_i32(neg_min), INT32_MIN);

  /* INT32_MIN / -1 overflows: result wraps to INT32_MIN */
  JaclVal div_min = jacl_div_i32(jacl_i32(INT32_MIN), jacl_i32(-1));
  ASSERT_INT_EQ(jacl_as_i32(div_min), INT32_MIN);

  /* INT32_MIN % -1 = 0 */
  JaclVal mod_min = jacl_mod_i32(jacl_i32(INT32_MIN), jacl_i32(-1));
  ASSERT_INT_EQ(jacl_as_i32(mod_min), 0);

  /* INT32_MAX + 1 wraps to INT32_MIN */
  JaclVal overflow = jacl_add_i32(jacl_i32(INT32_MAX), jacl_i32(1));
  ASSERT_INT_EQ(jacl_as_i32(overflow), INT32_MIN);

  /* INT32_MIN - 1 wraps to INT32_MAX */
  JaclVal underflow = jacl_sub_i32(jacl_i32(INT32_MIN), jacl_i32(1));
  ASSERT_INT_EQ(jacl_as_i32(underflow), INT32_MAX);

  TEST_PASS();
}

/* ---- US-011 tests: f32 arithmetic operations with flag propagation ---- */

/* Test: basic f32 arithmetic */
static int test_f32_arithmetic_basic(void) {
  /* 1.0 + 2.0 = 3.0 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_add_f32(jacl_f32(1.0f), jacl_f32(2.0f))), 3.0f));

  /* 10.0 - 3.0 = 7.0 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_sub_f32(jacl_f32(10.0f), jacl_f32(3.0f))), 7.0f));

  /* 6.0 * 7.0 = 42.0 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_mul_f32(jacl_f32(6.0f), jacl_f32(7.0f))), 42.0f));

  /* 10.0 / 4.0 = 2.5 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_div_f32(jacl_f32(10.0f), jacl_f32(4.0f))), 2.5f));

  /* All results should be f32-tagged */
  ASSERT(jacl_is_f32(jacl_add_f32(jacl_f32(1.0f), jacl_f32(2.0f))));
  ASSERT(jacl_is_f32(jacl_sub_f32(jacl_f32(5.0f), jacl_f32(3.0f))));
  ASSERT(jacl_is_f32(jacl_mul_f32(jacl_f32(2.0f), jacl_f32(3.0f))));
  ASSERT(jacl_is_f32(jacl_div_f32(jacl_f32(6.0f), jacl_f32(2.0f))));

  TEST_PASS();
}

/* Test: f32 arithmetic with negative numbers */
static int test_f32_arithmetic_negative(void) {
  /* -3.0 + 4.0 = 1.0 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_add_f32(jacl_f32(-3.0f), jacl_f32(4.0f))), 1.0f));

  /* 3.0 + (-4.0) = -1.0 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_add_f32(jacl_f32(3.0f), jacl_f32(-4.0f))), -1.0f));

  /* -3.0 - (-4.0) = 1.0 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_sub_f32(jacl_f32(-3.0f), jacl_f32(-4.0f))), 1.0f));

  /* -6.0 * 7.0 = -42.0 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_mul_f32(jacl_f32(-6.0f), jacl_f32(7.0f))), -42.0f));

  /* -6.0 * -7.0 = 42.0 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_mul_f32(jacl_f32(-6.0f), jacl_f32(-7.0f))), 42.0f));

  /* -10.0 / 4.0 = -2.5 */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_div_f32(jacl_f32(-10.0f), jacl_f32(4.0f))), -2.5f));

  TEST_PASS();
}

/* Test: jacl_neg_f32 */
static int test_f32_neg(void) {
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_neg_f32(jacl_f32(42.0f))), -42.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_neg_f32(jacl_f32(-42.0f))), 42.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_neg_f32(jacl_f32(0.0f))), -0.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_neg_f32(jacl_f32(-0.0f))), 0.0f));

  /* Result should be f32-tagged */
  ASSERT(jacl_is_f32(jacl_neg_f32(jacl_f32(42.0f))));

  TEST_PASS();
}

/* Test: inf and nan behavior (IEEE 754) */
static int test_f32_inf_nan(void) {
  /* inf + 1 = inf */
  float inf_plus = jacl_as_f32(jacl_add_f32(jacl_f32(INFINITY), jacl_f32(1.0f)));
  ASSERT(float_bitwise_eq(inf_plus, INFINITY));

  /* -inf + (-1) = -inf */
  float ninf_plus = jacl_as_f32(jacl_add_f32(jacl_f32(-INFINITY), jacl_f32(-1.0f)));
  ASSERT(float_bitwise_eq(ninf_plus, -INFINITY));

  /* 1.0 / 0.0 = inf (IEEE 754, NOT error-flagged) */
  JaclVal div_zero = jacl_div_f32(jacl_f32(1.0f), jacl_f32(0.0f));
  ASSERT(jacl_is_f32(div_zero));
  ASSERT(!jacl_is_error(div_zero));
  ASSERT(float_bitwise_eq(jacl_as_f32(div_zero), INFINITY));

  /* -1.0 / 0.0 = -inf */
  JaclVal ndiv_zero = jacl_div_f32(jacl_f32(-1.0f), jacl_f32(0.0f));
  ASSERT(!jacl_is_error(ndiv_zero));
  ASSERT(float_bitwise_eq(jacl_as_f32(ndiv_zero), -INFINITY));

  /* 0.0 / 0.0 = NaN */
  JaclVal zero_div_zero = jacl_div_f32(jacl_f32(0.0f), jacl_f32(0.0f));
  ASSERT(jacl_is_f32(zero_div_zero));
  ASSERT(!jacl_is_error(zero_div_zero));
  ASSERT(isnan(jacl_as_f32(zero_div_zero)));

  /* inf - inf = NaN */
  float inf_sub_inf = jacl_as_f32(jacl_sub_f32(jacl_f32(INFINITY), jacl_f32(INFINITY)));
  ASSERT(isnan(inf_sub_inf));

  /* inf * 0 = NaN */
  float inf_mul_zero = jacl_as_f32(jacl_mul_f32(jacl_f32(INFINITY), jacl_f32(0.0f)));
  ASSERT(isnan(inf_mul_zero));

  /* NaN + anything = NaN */
  float nan_add = jacl_as_f32(jacl_add_f32(jacl_f32(NAN), jacl_f32(1.0f)));
  ASSERT(isnan(nan_add));

  /* anything + NaN = NaN */
  float add_nan = jacl_as_f32(jacl_add_f32(jacl_f32(1.0f), jacl_f32(NAN)));
  ASSERT(isnan(add_nan));

  /* neg(inf) = -inf */
  ASSERT(float_bitwise_eq(jacl_as_f32(jacl_neg_f32(jacl_f32(INFINITY))), -INFINITY));

  /* neg(NaN) is NaN */
  ASSERT(isnan(jacl_as_f32(jacl_neg_f32(jacl_f32(NAN)))));

  TEST_PASS();
}

/* Test: error operand short-circuits (returns the error operand) */
static int test_f32_error_shortcircuit(void) {
  JaclVal err_a = jacl_set_error(jacl_f32(99.0f));
  JaclVal ok_b = jacl_f32(5.0f);

  /* Error on a: returns a */
  ASSERT_U64_EQ(jacl_add_f32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_sub_f32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_mul_f32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_div_f32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_neg_f32(err_a), err_a);

  /* Error on b: returns b */
  JaclVal err_b = jacl_set_error(jacl_f32(77.0f));
  ASSERT_U64_EQ(jacl_add_f32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_sub_f32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_mul_f32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_div_f32(ok_b, err_b), err_b);

  /* Both error: returns a (first checked) */
  ASSERT_U64_EQ(jacl_add_f32(err_a, err_b), err_a);

  TEST_PASS();
}

/* Test: flag propagation through f32 arithmetic */
static int test_f32_flag_propagation(void) {
  JaclVal tainted_a = jacl_set_tainted(jacl_f32(3.0f));
  JaclVal secret_b = jacl_set_secret(jacl_f32(4.0f));

  /* add: tainted + secret -> result has both flags */
  JaclVal sum = jacl_add_f32(tainted_a, secret_b);
  ASSERT(float_bitwise_eq(jacl_as_f32(sum), 7.0f));
  ASSERT(jacl_is_tainted(sum));
  ASSERT(jacl_is_secret(sum));
  ASSERT(!jacl_is_error(sum));

  /* sub: same flags propagate */
  JaclVal diff = jacl_sub_f32(tainted_a, secret_b);
  ASSERT(float_bitwise_eq(jacl_as_f32(diff), -1.0f));
  ASSERT(jacl_is_tainted(diff));
  ASSERT(jacl_is_secret(diff));

  /* mul: clean * tainted -> tainted */
  JaclVal prod = jacl_mul_f32(jacl_f32(2.0f), tainted_a);
  ASSERT(float_bitwise_eq(jacl_as_f32(prod), 6.0f));
  ASSERT(jacl_is_tainted(prod));
  ASSERT(!jacl_is_secret(prod));

  /* div: secret / clean -> secret */
  JaclVal quot = jacl_div_f32(secret_b, jacl_f32(2.0f));
  ASSERT(float_bitwise_eq(jacl_as_f32(quot), 2.0f));
  ASSERT(jacl_is_secret(quot));
  ASSERT(!jacl_is_tainted(quot));

  /* clean op clean -> no flags */
  JaclVal clean = jacl_add_f32(jacl_f32(1.0f), jacl_f32(2.0f));
  ASSERT(!jacl_is_tainted(clean));
  ASSERT(!jacl_is_secret(clean));
  ASSERT(!jacl_is_error(clean));

  TEST_PASS();
}

/* Test: neg preserves flags */
static int test_f32_neg_preserves_flags(void) {
  JaclVal v = jacl_set_tainted(jacl_set_secret(jacl_f32(42.0f)));
  JaclVal neg = jacl_neg_f32(v);

  ASSERT(float_bitwise_eq(jacl_as_f32(neg), -42.0f));
  ASSERT(jacl_is_tainted(neg));
  ASSERT(jacl_is_secret(neg));
  ASSERT(!jacl_is_error(neg));

  TEST_PASS();
}

/* ---- US-012 tests: Comparison operations with flag propagation ---- */

/* Test: jacl_eq same-type equality */
static int test_eq_same_type(void) {
  /* nil == nil */
  ASSERT(jacl_as_bool(jacl_eq(JACL_NIL, JACL_NIL)));

  /* true == true */
  ASSERT(jacl_as_bool(jacl_eq(JACL_TRUE, JACL_TRUE)));

  /* false == false */
  ASSERT(jacl_as_bool(jacl_eq(JACL_FALSE, JACL_FALSE)));

  /* true != false */
  ASSERT(!jacl_as_bool(jacl_eq(JACL_TRUE, JACL_FALSE)));

  /* i32: 42 == 42, 42 != 43 */
  ASSERT(jacl_as_bool(jacl_eq(jacl_i32(42), jacl_i32(42))));
  ASSERT(!jacl_as_bool(jacl_eq(jacl_i32(42), jacl_i32(43))));

  /* i32: negative */
  ASSERT(jacl_as_bool(jacl_eq(jacl_i32(-1), jacl_i32(-1))));
  ASSERT(!jacl_as_bool(jacl_eq(jacl_i32(-1), jacl_i32(1))));

  /* f32: 3.14 == 3.14, 3.14 != 2.71 */
  ASSERT(jacl_as_bool(jacl_eq(jacl_f32(3.14f), jacl_f32(3.14f))));
  ASSERT(!jacl_as_bool(jacl_eq(jacl_f32(3.14f), jacl_f32(2.71f))));

  /* Result should be bool-tagged */
  ASSERT(jacl_is_bool(jacl_eq(jacl_i32(1), jacl_i32(1))));

  TEST_PASS();
}

/* Test: cross-type comparisons return false */
static int test_eq_cross_type(void) {
  /* nil != false (different type tags) */
  ASSERT(!jacl_as_bool(jacl_eq(JACL_NIL, JACL_FALSE)));

  /* nil != 0 (i32) */
  ASSERT(!jacl_as_bool(jacl_eq(JACL_NIL, jacl_i32(0))));

  /* false != 0 (i32) */
  ASSERT(!jacl_as_bool(jacl_eq(JACL_FALSE, jacl_i32(0))));

  /* i32(1) != f32(1.0) */
  ASSERT(!jacl_as_bool(jacl_eq(jacl_i32(1), jacl_f32(1.0f))));

  /* i32(0) != nil */
  ASSERT(!jacl_as_bool(jacl_eq(jacl_i32(0), JACL_NIL)));

  /* inline string != string pointer (different type tags even if same content) */
  int dummy;
  ASSERT(!jacl_as_bool(jacl_eq(jacl_inline_string("hi", 2), jacl_string_ptr(&dummy))));

  TEST_PASS();
}

/* Test: jacl_eq for inline strings compares payload bytes */
static int test_eq_inline_string(void) {
  JaclVal a = jacl_inline_string("hello", 5);
  JaclVal b = jacl_inline_string("hello", 5);
  JaclVal c = jacl_inline_string("world", 5);
  JaclVal d = jacl_inline_string("hell", 4);
  JaclVal e = jacl_inline_string("", 0);
  JaclVal f = jacl_inline_string("", 0);

  ASSERT(jacl_as_bool(jacl_eq(a, b)));
  ASSERT(!jacl_as_bool(jacl_eq(a, c)));
  ASSERT(!jacl_as_bool(jacl_eq(a, d)));
  ASSERT(jacl_as_bool(jacl_eq(e, f)));
  ASSERT(!jacl_as_bool(jacl_eq(e, a)));

  TEST_PASS();
}

/* Test: jacl_eq for pointer types compares pointer values */
static int test_eq_pointer(void) {
  int x, y;
  JaclVal a = jacl_string_ptr(&x);
  JaclVal b = jacl_string_ptr(&x);  /* same pointer */
  JaclVal c = jacl_string_ptr(&y);  /* different pointer */

  ASSERT(jacl_as_bool(jacl_eq(a, b)));
  ASSERT(!jacl_as_bool(jacl_eq(a, c)));

  /* Different pointer types with same address are not equal (different type tags) */
  JaclVal vec = jacl_vector_ptr(&x);
  ASSERT(!jacl_as_bool(jacl_eq(a, vec)));

  TEST_PASS();
}

/* Test: jacl_eq ignores flags when comparing */
static int test_eq_ignores_flags(void) {
  JaclVal a = jacl_set_tainted(jacl_i32(42));
  JaclVal b = jacl_i32(42);

  /* Same type+payload, different flags -> equal */
  ASSERT(jacl_as_bool(jacl_eq(a, b)));

  JaclVal c = jacl_set_secret(jacl_set_tainted(jacl_i32(42)));
  ASSERT(jacl_as_bool(jacl_eq(a, c)));

  TEST_PASS();
}

/* Test: i32 ordering comparisons */
static int test_cmp_i32_ordering(void) {
  JaclVal a = jacl_i32(3);
  JaclVal b = jacl_i32(7);
  JaclVal c = jacl_i32(3);

  /* lt */
  ASSERT(jacl_as_bool(jacl_lt_i32(a, b)));
  ASSERT(!jacl_as_bool(jacl_lt_i32(b, a)));
  ASSERT(!jacl_as_bool(jacl_lt_i32(a, c)));  /* equal -> false */

  /* gt */
  ASSERT(!jacl_as_bool(jacl_gt_i32(a, b)));
  ASSERT(jacl_as_bool(jacl_gt_i32(b, a)));
  ASSERT(!jacl_as_bool(jacl_gt_i32(a, c)));  /* equal -> false */

  /* le */
  ASSERT(jacl_as_bool(jacl_le_i32(a, b)));
  ASSERT(!jacl_as_bool(jacl_le_i32(b, a)));
  ASSERT(jacl_as_bool(jacl_le_i32(a, c)));   /* equal -> true */

  /* ge */
  ASSERT(!jacl_as_bool(jacl_ge_i32(a, b)));
  ASSERT(jacl_as_bool(jacl_ge_i32(b, a)));
  ASSERT(jacl_as_bool(jacl_ge_i32(a, c)));   /* equal -> true */

  /* Negative numbers: signed comparison */
  JaclVal neg = jacl_i32(-5);
  JaclVal pos = jacl_i32(5);
  ASSERT(jacl_as_bool(jacl_lt_i32(neg, pos)));
  ASSERT(!jacl_as_bool(jacl_gt_i32(neg, pos)));

  /* INT32_MIN < INT32_MAX */
  ASSERT(jacl_as_bool(jacl_lt_i32(jacl_i32(INT32_MIN), jacl_i32(INT32_MAX))));
  ASSERT(jacl_as_bool(jacl_gt_i32(jacl_i32(INT32_MAX), jacl_i32(INT32_MIN))));

  /* All results should be bool-tagged */
  ASSERT(jacl_is_bool(jacl_lt_i32(a, b)));
  ASSERT(jacl_is_bool(jacl_gt_i32(a, b)));
  ASSERT(jacl_is_bool(jacl_le_i32(a, b)));
  ASSERT(jacl_is_bool(jacl_ge_i32(a, b)));

  TEST_PASS();
}

/* Test: f32 ordering comparisons */
static int test_cmp_f32_ordering(void) {
  JaclVal a = jacl_f32(1.5f);
  JaclVal b = jacl_f32(2.5f);
  JaclVal c = jacl_f32(1.5f);

  /* lt */
  ASSERT(jacl_as_bool(jacl_lt_f32(a, b)));
  ASSERT(!jacl_as_bool(jacl_lt_f32(b, a)));
  ASSERT(!jacl_as_bool(jacl_lt_f32(a, c)));

  /* gt */
  ASSERT(!jacl_as_bool(jacl_gt_f32(a, b)));
  ASSERT(jacl_as_bool(jacl_gt_f32(b, a)));
  ASSERT(!jacl_as_bool(jacl_gt_f32(a, c)));

  /* le */
  ASSERT(jacl_as_bool(jacl_le_f32(a, b)));
  ASSERT(!jacl_as_bool(jacl_le_f32(b, a)));
  ASSERT(jacl_as_bool(jacl_le_f32(a, c)));

  /* ge */
  ASSERT(!jacl_as_bool(jacl_ge_f32(a, b)));
  ASSERT(jacl_as_bool(jacl_ge_f32(b, a)));
  ASSERT(jacl_as_bool(jacl_ge_f32(a, c)));

  /* Negative numbers */
  JaclVal neg = jacl_f32(-1.0f);
  JaclVal pos = jacl_f32(1.0f);
  ASSERT(jacl_as_bool(jacl_lt_f32(neg, pos)));
  ASSERT(!jacl_as_bool(jacl_gt_f32(neg, pos)));

  /* NaN comparisons: all return false (IEEE 754) */
  JaclVal nan_v = jacl_f32(NAN);
  ASSERT(!jacl_as_bool(jacl_lt_f32(nan_v, a)));
  ASSERT(!jacl_as_bool(jacl_gt_f32(nan_v, a)));
  ASSERT(!jacl_as_bool(jacl_le_f32(nan_v, a)));
  ASSERT(!jacl_as_bool(jacl_ge_f32(nan_v, a)));
  ASSERT(!jacl_as_bool(jacl_lt_f32(a, nan_v)));
  ASSERT(!jacl_as_bool(jacl_gt_f32(a, nan_v)));
  ASSERT(!jacl_as_bool(jacl_le_f32(a, nan_v)));
  ASSERT(!jacl_as_bool(jacl_ge_f32(a, nan_v)));

  /* inf comparisons */
  JaclVal inf_v = jacl_f32(INFINITY);
  ASSERT(jacl_as_bool(jacl_lt_f32(a, inf_v)));
  ASSERT(jacl_as_bool(jacl_gt_f32(inf_v, a)));

  /* All results should be bool-tagged */
  ASSERT(jacl_is_bool(jacl_lt_f32(a, b)));

  TEST_PASS();
}

/* Test: flag propagation on comparison results */
static int test_cmp_flag_propagation(void) {
  JaclVal tainted_a = jacl_set_tainted(jacl_i32(3));
  JaclVal secret_b = jacl_set_secret(jacl_i32(7));

  /* eq: tainted + secret -> result has both flags */
  JaclVal eq_result = jacl_eq(tainted_a, secret_b);
  ASSERT(!jacl_as_bool(eq_result));  /* 3 != 7 */
  ASSERT(jacl_is_tainted(eq_result));
  ASSERT(jacl_is_secret(eq_result));
  ASSERT(!jacl_is_error(eq_result));

  /* lt_i32: tainted < secret -> result has both flags */
  JaclVal lt_result = jacl_lt_i32(tainted_a, secret_b);
  ASSERT(jacl_as_bool(lt_result));  /* 3 < 7 */
  ASSERT(jacl_is_tainted(lt_result));
  ASSERT(jacl_is_secret(lt_result));

  /* gt_i32: clean > clean -> no flags */
  JaclVal gt_result = jacl_gt_i32(jacl_i32(10), jacl_i32(5));
  ASSERT(jacl_as_bool(gt_result));
  ASSERT(!jacl_is_tainted(gt_result));
  ASSERT(!jacl_is_secret(gt_result));
  ASSERT(!jacl_is_error(gt_result));

  /* f32 comparisons propagate flags too */
  JaclVal tainted_f = jacl_set_tainted(jacl_f32(1.0f));
  JaclVal secret_f = jacl_set_secret(jacl_f32(2.0f));
  JaclVal lt_f_result = jacl_lt_f32(tainted_f, secret_f);
  ASSERT(jacl_as_bool(lt_f_result));
  ASSERT(jacl_is_tainted(lt_f_result));
  ASSERT(jacl_is_secret(lt_f_result));

  TEST_PASS();
}

/* Test: error short-circuit on comparisons */
static int test_cmp_error_shortcircuit(void) {
  JaclVal err_a = jacl_set_error(jacl_i32(99));
  JaclVal ok_b = jacl_i32(5);

  /* Error on a: returns a */
  ASSERT_U64_EQ(jacl_eq(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_lt_i32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_gt_i32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_le_i32(err_a, ok_b), err_a);
  ASSERT_U64_EQ(jacl_ge_i32(err_a, ok_b), err_a);

  /* Error on b: returns b */
  JaclVal err_b = jacl_set_error(jacl_i32(77));
  ASSERT_U64_EQ(jacl_eq(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_lt_i32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_gt_i32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_le_i32(ok_b, err_b), err_b);
  ASSERT_U64_EQ(jacl_ge_i32(ok_b, err_b), err_b);

  /* Both error: returns a (first checked) */
  ASSERT_U64_EQ(jacl_eq(err_a, err_b), err_a);
  ASSERT_U64_EQ(jacl_lt_i32(err_a, err_b), err_a);

  /* f32 error short-circuit */
  JaclVal err_f = jacl_set_error(jacl_f32(99.0f));
  JaclVal ok_f = jacl_f32(5.0f);
  ASSERT_U64_EQ(jacl_lt_f32(err_f, ok_f), err_f);
  ASSERT_U64_EQ(jacl_gt_f32(err_f, ok_f), err_f);
  ASSERT_U64_EQ(jacl_le_f32(err_f, ok_f), err_f);
  ASSERT_U64_EQ(jacl_ge_f32(err_f, ok_f), err_f);
  ASSERT_U64_EQ(jacl_lt_f32(ok_f, err_f), err_f);
  ASSERT_U64_EQ(jacl_gt_f32(ok_f, err_f), err_f);
  ASSERT_U64_EQ(jacl_le_f32(ok_f, err_f), err_f);
  ASSERT_U64_EQ(jacl_ge_f32(ok_f, err_f), err_f);

  TEST_PASS();
}

/* Test: nil comparisons */
static int test_eq_nil(void) {
  /* nil == nil */
  ASSERT(jacl_as_bool(jacl_eq(JACL_NIL, JACL_NIL)));

  /* nil != anything else */
  ASSERT(!jacl_as_bool(jacl_eq(JACL_NIL, JACL_TRUE)));
  ASSERT(!jacl_as_bool(jacl_eq(JACL_NIL, JACL_FALSE)));
  ASSERT(!jacl_as_bool(jacl_eq(JACL_NIL, jacl_i32(0))));
  ASSERT(!jacl_as_bool(jacl_eq(JACL_NIL, jacl_f32(0.0f))));
  ASSERT(!jacl_as_bool(jacl_eq(JACL_NIL, jacl_inline_string("", 0))));

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

  /* US-006: Inline string packing and unpacking */
  printf("Test: inline string empty\n");
  if (!test_inline_string_empty()) return 1;

  printf("Test: inline string single char\n");
  if (!test_inline_string_single_char()) return 1;

  printf("Test: inline string 6 bytes\n");
  if (!test_inline_string_6_bytes()) return 1;

  printf("Test: inline string 7 bytes\n");
  if (!test_inline_string_7_bytes()) return 1;

  printf("Test: inline string round-trip\n");
  if (!test_inline_string_roundtrip()) return 1;

  printf("Test: inline string byte values\n");
  if (!test_inline_string_byte_values()) return 1;

  printf("Test: inline string vs string tag\n");
  if (!test_inline_string_vs_string_tag()) return 1;

  printf("Test: inline string predicate with flags\n");
  if (!test_inline_string_predicate_with_flags()) return 1;

  printf("Test: inline string null terminates\n");
  if (!test_inline_string_null_terminates()) return 1;

  /* US-007: Tainted/secret/error flag manipulation */
  printf("Test: tainted flag\n");
  if (!test_flag_tainted()) return 1;

  printf("Test: secret flag\n");
  if (!test_flag_secret()) return 1;

  printf("Test: error flag\n");
  if (!test_flag_error()) return 1;

  printf("Test: flags preserve type and payload\n");
  if (!test_flag_preserves_type_payload()) return 1;

  printf("Test: clear preserves type and payload\n");
  if (!test_clear_preserves_type_payload()) return 1;

  printf("Test: multiple flags simultaneously\n");
  if (!test_flag_multiple()) return 1;

  printf("Test: flag independence\n");
  if (!test_flag_independence()) return 1;

  printf("Test: flags with all value types\n");
  if (!test_flags_all_types()) return 1;

  printf("Test: flag idempotent\n");
  if (!test_flag_idempotent()) return 1;

  /* US-008: Flag propagation helpers */
  printf("Test: propagate flags basic\n");
  if (!test_propagate_flags_basic()) return 1;

  printf("Test: propagate flags none\n");
  if (!test_propagate_flags_none()) return 1;

  printf("Test: propagate flags one each\n");
  if (!test_propagate_flags_one_each()) return 1;

  printf("Test: propagate flags all\n");
  if (!test_propagate_flags_all()) return 1;

  printf("Test: propagate flags mixed\n");
  if (!test_propagate_flags_mixed()) return 1;

  printf("Test: propagate flags error either\n");
  if (!test_propagate_flags_error_either()) return 1;

  printf("Test: apply flags basic\n");
  if (!test_apply_flags_basic()) return 1;

  printf("Test: apply flags zero\n");
  if (!test_apply_flags_zero()) return 1;

  printf("Test: apply flags preserves existing\n");
  if (!test_apply_flags_preserves_existing()) return 1;

  printf("Test: apply flags masks input\n");
  if (!test_apply_flags_masks_input()) return 1;

  printf("Test: propagate and apply end-to-end\n");
  if (!test_propagate_and_apply()) return 1;

  printf("Test: propagate flags combinations\n");
  if (!test_propagate_flags_combinations()) return 1;

  /* US-009: Complete type predicate set and type_tag extractor */
  printf("Test: type_tag extractor\n");
  if (!test_type_tag_extractor()) return 1;

  printf("Test: predicate NxN matrix\n");
  if (!test_predicate_matrix()) return 1;

  printf("Test: predicate matrix with flags\n");
  if (!test_predicate_matrix_with_flags()) return 1;

  /* US-010: i32 arithmetic operations */
  printf("Test: i32 arithmetic basic\n");
  if (!test_i32_arithmetic_basic()) return 1;

  printf("Test: i32 arithmetic negative\n");
  if (!test_i32_arithmetic_negative()) return 1;

  printf("Test: i32 neg\n");
  if (!test_i32_neg()) return 1;

  printf("Test: i32 div by zero\n");
  if (!test_i32_div_by_zero()) return 1;

  printf("Test: i32 mod by zero\n");
  if (!test_i32_mod_by_zero()) return 1;

  printf("Test: i32 error short-circuit\n");
  if (!test_i32_error_shortcircuit()) return 1;

  printf("Test: i32 flag propagation\n");
  if (!test_i32_flag_propagation()) return 1;

  printf("Test: i32 neg preserves flags\n");
  if (!test_i32_neg_preserves_flags()) return 1;

  printf("Test: i32 div/mod zero flag propagation\n");
  if (!test_i32_divmod_zero_flag_propagation()) return 1;

  printf("Test: i32 INT32_MIN edge cases\n");
  if (!test_i32_int32_min_edges()) return 1;

  /* US-011: f32 arithmetic operations */
  printf("Test: f32 arithmetic basic\n");
  if (!test_f32_arithmetic_basic()) return 1;

  printf("Test: f32 arithmetic negative\n");
  if (!test_f32_arithmetic_negative()) return 1;

  printf("Test: f32 neg\n");
  if (!test_f32_neg()) return 1;

  printf("Test: f32 inf/nan behavior\n");
  if (!test_f32_inf_nan()) return 1;

  printf("Test: f32 error short-circuit\n");
  if (!test_f32_error_shortcircuit()) return 1;

  printf("Test: f32 flag propagation\n");
  if (!test_f32_flag_propagation()) return 1;

  printf("Test: f32 neg preserves flags\n");
  if (!test_f32_neg_preserves_flags()) return 1;

  /* US-012: Comparison operations */
  printf("Test: eq same type\n");
  if (!test_eq_same_type()) return 1;

  printf("Test: eq cross type\n");
  if (!test_eq_cross_type()) return 1;

  printf("Test: eq inline string\n");
  if (!test_eq_inline_string()) return 1;

  printf("Test: eq pointer\n");
  if (!test_eq_pointer()) return 1;

  printf("Test: eq ignores flags\n");
  if (!test_eq_ignores_flags()) return 1;

  printf("Test: i32 ordering comparisons\n");
  if (!test_cmp_i32_ordering()) return 1;

  printf("Test: f32 ordering comparisons\n");
  if (!test_cmp_f32_ordering()) return 1;

  printf("Test: comparison flag propagation\n");
  if (!test_cmp_flag_propagation()) return 1;

  printf("Test: comparison error short-circuit\n");
  if (!test_cmp_error_shortcircuit()) return 1;

  printf("Test: eq nil\n");
  if (!test_eq_nil()) return 1;

  return 0;
}
