/* builtins.c — JACL runtime builtins (P1.7): the dynamic value-op layer that
 * Phase-2 codegen lowers operator/builtin calls to.
 *
 * Error model (mirrors src/value.c): an error operand short-circuits (returned as
 * is); a type/domain error returns an error-flagged value. Tainted/secret flags
 * propagate as the union of the operands'. Ordered comparisons are i32-only for
 * now (string ordering, f64 arithmetic, etc. continue into Phase 2). This is a
 * representative core, not the full opcode surface.
 */
#include "jaclrt.h"

static inline uint64_t prop_flags(JaclVal a, JaclVal b) {
  return (a | b) & (JACL_FLAG_TAINTED | JACL_FLAG_SECRET);
}
#define ERR_IF_ERR(a, b) do { if (jaclrt_is_error(a)) return (a); if (jaclrt_is_error(b)) return (b); } while (0)

/* ---- arithmetic (i32) ---- */
JaclVal jacl_add(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  return jaclrt_i32(jaclrt_as_i32(a) + jaclrt_as_i32(b)) | prop_flags(a, b);
}
JaclVal jacl_sub(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  return jaclrt_i32(jaclrt_as_i32(a) - jaclrt_as_i32(b)) | prop_flags(a, b);
}
JaclVal jacl_mul(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  return jaclrt_i32(jaclrt_as_i32(a) * jaclrt_as_i32(b)) | prop_flags(a, b);
}
JaclVal jacl_div(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  int32_t x = jaclrt_as_i32(a), y = jaclrt_as_i32(b);
  if (y == 0) return jaclrt_set_error(jaclrt_i32(0)) | prop_flags(a, b);
  int32_t q = (x == INT32_MIN && y == -1) ? INT32_MIN : x / y;   /* avoid UB overflow */
  return jaclrt_i32(q) | prop_flags(a, b);
}
JaclVal jacl_mod(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  int32_t x = jaclrt_as_i32(a), y = jaclrt_as_i32(b);
  if (y == 0) return jaclrt_set_error(jaclrt_i32(0)) | prop_flags(a, b);
  int32_t r = (x == INT32_MIN && y == -1) ? 0 : x % y;
  return jaclrt_i32(r) | prop_flags(a, b);
}
JaclVal jacl_neg(JaclVal a) {
  if (jaclrt_is_error(a)) return a;
  if (!jaclrt_is_i32(a)) return jaclrt_error();
  return jaclrt_i32(-jaclrt_as_i32(a)) | (a & (JACL_FLAG_TAINTED | JACL_FLAG_SECRET));
}

/* ---- comparisons ---- */
JaclVal jacl_eq(JaclVal a, JaclVal b) {                 /* bitwise type+payload (mirrors value.c) */
  ERR_IF_ERR(a, b);
  uint64_t mask = JACL_TYPE_MASK | JACL_PAYLOAD_MASK;
  return jaclrt_bool((a & mask) == (b & mask)) | prop_flags(a, b);
}
JaclVal jacl_ne(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  uint64_t mask = JACL_TYPE_MASK | JACL_PAYLOAD_MASK;
  return jaclrt_bool((a & mask) != (b & mask)) | prop_flags(a, b);
}
JaclVal jacl_lt(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  return jaclrt_bool(jaclrt_as_i32(a) < jaclrt_as_i32(b)) | prop_flags(a, b);
}
JaclVal jacl_le(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  return jaclrt_bool(jaclrt_as_i32(a) <= jaclrt_as_i32(b)) | prop_flags(a, b);
}
JaclVal jacl_gt(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  return jaclrt_bool(jaclrt_as_i32(a) > jaclrt_as_i32(b)) | prop_flags(a, b);
}
JaclVal jacl_ge(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  return jaclrt_bool(jaclrt_as_i32(a) >= jaclrt_as_i32(b)) | prop_flags(a, b);
}
JaclVal jacl_not(JaclVal a) {
  if (jaclrt_is_error(a)) return a;
  if (!jaclrt_is_bool(a)) return jaclrt_error();
  return jaclrt_bool(!jaclrt_as_bool(a));
}

/* ---- type ops ---- */
JaclVal jacl_typeof(JaclVal v) {
  switch (jaclrt_type_index(v)) {        /* switch on the i32 type index (svm-llvm OK) */
    case 0x00: return jacl_str_new("nil", 3);
    case 0x01: return jacl_str_new("bool", 4);
    case 0x02: return jacl_str_new("i32", 3);
    case 0x03: return jacl_str_new("f32", 3);
    case 0x04: case 0x05: case 0x14: return jacl_str_new("string", 6);
    case 0x06: return jacl_str_new("vec", 3);
    case 0x07: return jacl_str_new("map", 3);
    case 0x15: return jacl_str_new("stream", 6);
    default:   return jacl_str_new("value", 5);
  }
}
JaclVal jacl_len(JaclVal v) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_is_string(v)) return jaclrt_i32((int32_t)jacl_str_len(v));
  uint32_t t = jaclrt_type_index(v);
  if (t == 0x06) return jaclrt_i32((int32_t)jacl_vec_count(v));   /* VECTOR */
  if (t == 0x07) return jaclrt_i32((int32_t)jacl_map_count(v));   /* MAP    */
  return jaclrt_error();
}
JaclVal jacl_to_string(JaclVal v) {
  if (jaclrt_is_string(v)) return v;
  switch (jaclrt_type_index(v)) {
    case 0x00: return jacl_str_new("nil", 3);
    case 0x01: return jaclrt_as_bool(v) ? jacl_str_new("true", 4) : jacl_str_new("false", 5);
    case 0x02: {
      char tmp[16]; int j = 0;
      int32_t x = jaclrt_as_i32(v);
      int neg = x < 0;
      uint32_t u = neg ? (uint32_t)(-(int64_t)x) : (uint32_t)x;
      if (u == 0) tmp[j++] = '0';
      while (u) { tmp[j++] = (char)('0' + (u % 10)); u /= 10; }
      char out[16]; int i = 0;
      if (neg) out[i++] = '-';
      while (j) out[i++] = tmp[--j];
      return jacl_str_new(out, (uint32_t)i);
    }
    default: return jaclrt_error();
  }
}
