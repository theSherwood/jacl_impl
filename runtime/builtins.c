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
/* ---- inline f32 (tag 0x03, bits in the low payload word) ---- */
static inline JaclVal jaclrt_f32v(float f) {
  union { float f; uint32_t u; } c; c.f = f;
  return JACL_TAG_F32 | (uint64_t)c.u;
}
static inline float jaclrt_as_f32(JaclVal v) {
  union { float f; uint32_t u; } c; c.u = (uint32_t)(v & 0xFFFFFFFFu);
  return c.f;
}
static inline int jacl_is_num(JaclVal v) {
  uint32_t t = jaclrt_type_index(v);
  return t == 0x02 || t == 0x03;
}
static inline float jacl_num_f32(JaclVal v) {
  return jaclrt_is_i32(v) ? (float)jaclrt_as_i32(v) : jaclrt_as_f32(v);
}
/* ---- wide numbers (heap cells: i64 0x0E, u64 0x0F, f64 0x10 — 8 payload bytes) ---- */
JaclVal jacl_wide_new(uint32_t tidx, int64_t bits) {
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_BLOB, 8);
  *(int64_t *)jacl_obj_payload(o) = bits;
  return jaclrt_from_ptr((uint64_t)tidx << JACL_TAG_SHIFT, o);
}
static inline int64_t jacl_wide_bits(JaclVal v) {
  return *(int64_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(v));
}
static inline int jacl_is_iwide(JaclVal v) {
  uint32_t t = jaclrt_type_index(v);
  return t == 0x0E || t == 0x0F;
}
static inline int jacl_is_anyint(JaclVal v) { return jaclrt_is_i32(v) || jacl_is_iwide(v); }
static inline int64_t jacl_int_val(JaclVal v) {
  return jaclrt_is_i32(v) ? (int64_t)jaclrt_as_i32(v) : jacl_wide_bits(v);
}
static inline int jacl_is_f64(JaclVal v) { return jaclrt_type_index(v) == 0x10; }
static inline int jacl_is_anyfloat(JaclVal v) { return jaclrt_type_index(v) == 0x03 || jacl_is_f64(v); }
static inline double jacl_num_f64(JaclVal v) {
  if (jaclrt_is_i32(v)) return (double)jaclrt_as_i32(v);
  if (jaclrt_type_index(v) == 0x03) return (double)jaclrt_as_f32(v);
  if (jacl_is_f64(v)) { union { int64_t b; double d; } c; c.b = jacl_wide_bits(v); return c.d; }
  return (double)jacl_wide_bits(v);
}
static JaclVal jacl_f64_new(double d) {
  union { double d; int64_t b; } c; c.d = d;
  return jacl_wide_new(0x10, c.b);
}
/* result tag for a wide-int binop: u64 if either side is u64, else i64 */
static inline uint32_t jacl_iwide_tag(JaclVal a, JaclVal b) {
  return (jaclrt_type_index(a) == 0x0F || jaclrt_type_index(b) == 0x0F) ? 0x0F : 0x0E;
}
#define ERR_IF_ERR(a, b) do { if (jaclrt_is_error(a)) return (a); if (jaclrt_is_error(b)) return (b); } while (0)

/* ---- arithmetic (i32) ---- */
JaclVal jacl_add(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (jaclrt_is_i32(a) && jaclrt_is_i32(b)) {
    int32_t r;
    if (!__builtin_add_overflow(jaclrt_as_i32(a), jaclrt_as_i32(b), &r))
      return jaclrt_i32(r) | prop_flags(a, b);
    return jacl_wide_new(0x0E, (int64_t)jaclrt_as_i32(a) + (int64_t)jaclrt_as_i32(b)) | prop_flags(a, b);
  }
  if ((jacl_is_f64(a) && (jacl_is_anyfloat(b) || jacl_is_anyint(b))) ||
      (jacl_is_f64(b) && (jacl_is_anyfloat(a) || jacl_is_anyint(a))))
    return jacl_f64_new(jacl_num_f64(a) + jacl_num_f64(b)) | prop_flags(a, b);
  if (jacl_is_anyint(a) && jacl_is_anyint(b) && (jacl_is_iwide(a) || jacl_is_iwide(b)))
    return jacl_wide_new(jacl_iwide_tag(a, b), jacl_int_val(a) + jacl_int_val(b)) | prop_flags(a, b);
  if (jacl_is_num(a) && jacl_is_num(b))
    return jaclrt_f32v(jacl_num_f32(a) + jacl_num_f32(b)) | prop_flags(a, b);
  return jaclrt_error();
}
JaclVal jacl_sub(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (jaclrt_is_i32(a) && jaclrt_is_i32(b)) {
    int32_t r;
    if (!__builtin_sub_overflow(jaclrt_as_i32(a), jaclrt_as_i32(b), &r))
      return jaclrt_i32(r) | prop_flags(a, b);
    return jacl_wide_new(0x0E, (int64_t)jaclrt_as_i32(a) - (int64_t)jaclrt_as_i32(b)) | prop_flags(a, b);
  }
  if ((jacl_is_f64(a) && (jacl_is_anyfloat(b) || jacl_is_anyint(b))) ||
      (jacl_is_f64(b) && (jacl_is_anyfloat(a) || jacl_is_anyint(a))))
    return jacl_f64_new(jacl_num_f64(a) - jacl_num_f64(b)) | prop_flags(a, b);
  if (jacl_is_anyint(a) && jacl_is_anyint(b) && (jacl_is_iwide(a) || jacl_is_iwide(b)))
    return jacl_wide_new(jacl_iwide_tag(a, b), jacl_int_val(a) - jacl_int_val(b)) | prop_flags(a, b);
  if (jacl_is_num(a) && jacl_is_num(b))
    return jaclrt_f32v(jacl_num_f32(a) - jacl_num_f32(b)) | prop_flags(a, b);
  return jaclrt_error();
}
JaclVal jacl_mul(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (jaclrt_is_i32(a) && jaclrt_is_i32(b)) {
    int32_t r;
    if (!__builtin_mul_overflow(jaclrt_as_i32(a), jaclrt_as_i32(b), &r))
      return jaclrt_i32(r) | prop_flags(a, b);
    return jacl_wide_new(0x0E, (int64_t)jaclrt_as_i32(a) * (int64_t)jaclrt_as_i32(b)) | prop_flags(a, b);
  }
  if ((jacl_is_f64(a) && (jacl_is_anyfloat(b) || jacl_is_anyint(b))) ||
      (jacl_is_f64(b) && (jacl_is_anyfloat(a) || jacl_is_anyint(a))))
    return jacl_f64_new(jacl_num_f64(a) * jacl_num_f64(b)) | prop_flags(a, b);
  if (jacl_is_anyint(a) && jacl_is_anyint(b) && (jacl_is_iwide(a) || jacl_is_iwide(b)))
    return jacl_wide_new(jacl_iwide_tag(a, b), jacl_int_val(a) * jacl_int_val(b)) | prop_flags(a, b);
  if (jacl_is_num(a) && jacl_is_num(b))
    return jaclrt_f32v(jacl_num_f32(a) * jacl_num_f32(b)) | prop_flags(a, b);
  return jaclrt_error();
}
JaclVal jacl_div(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if ((jacl_is_f64(a) || jacl_is_f64(b)) && (jacl_is_anyint(a) || jacl_is_anyfloat(a)) &&
      (jacl_is_anyint(b) || jacl_is_anyfloat(b)))
    return jacl_f64_new(jacl_num_f64(a) / jacl_num_f64(b)) | prop_flags(a, b);
  if (jacl_is_anyint(a) && jacl_is_anyint(b) && (jacl_is_iwide(a) || jacl_is_iwide(b))) {
    int64_t y = jacl_int_val(b);
    if (y == 0) return jaclrt_set_error(jaclrt_i32(0)) | prop_flags(a, b);
    return jacl_wide_new(jacl_iwide_tag(a, b), jacl_int_val(a) / y) | prop_flags(a, b);
  }
  if (jacl_is_num(a) && jacl_is_num(b) && !(jaclrt_is_i32(a) && jaclrt_is_i32(b)))
    return jaclrt_f32v(jacl_num_f32(a) / jacl_num_f32(b)) | prop_flags(a, b);
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
/* Structural equality: strings by content, vectors elementwise, maps by content
 * (order-independent; both must have the same count and every (k,v) of `a` must
 * match in `b`), everything else bitwise type+payload. Recursive, so nested
 * collections compare deeply. Also the map key-equality handler (jmap_key_eq). */
#define JACL_EQ_MAP_CAP 64
int jacl_val_equal(JaclVal a, JaclVal b) {
  uint64_t mask = JACL_TYPE_MASK | JACL_PAYLOAD_MASK;
  if (((a ^ b) & mask) == 0) return 1;
  if (jaclrt_is_string(a) && jaclrt_is_string(b)) return jacl_str_eq(a, b);
  if ((jacl_is_anyint(a) || jacl_is_anyfloat(a)) && (jacl_is_anyint(b) || jacl_is_anyfloat(b))) {
    if (jacl_is_anyint(a) && jacl_is_anyint(b)) return jacl_int_val(a) == jacl_int_val(b);
    return jacl_num_f64(a) == jacl_num_f64(b);
  }
  uint32_t t = jaclrt_type_index(a), tb2 = jaclrt_type_index(b);
  /* A typed vector `[Vec T]` (0x1B) shares the vector representation and compares equal to
   * a dynamic vector (0x06) with the same elements — normalize both to the vector kind. */
  if (t == 0x1B) t = 0x06;
  if (tb2 == 0x1B) tb2 = 0x06;
  if (t != tb2) return 0;
  if (t == 0x06) {                                       /* VECTOR (dynamic or typed) */
    uint32_t n = jacl_vec_count(a);
    if (n != jacl_vec_count(b)) return 0;
    for (uint32_t i = 0; i < n; i++)
      if (!jacl_val_equal(jacl_vec_get(a, i), jacl_vec_get(b, i))) return 0;
    return 1;
  }
  /* ARR (0x1A) is a mutable REFERENCE type: `==` is identity, not structural (the
   * fast-path pointer compare above already returns 1 for the same object), so two
   * distinct arrays with equal contents are NOT equal. See ARR_DESIGN.md. */
  if (t == 0x12) {                                       /* STRUCT */
    JaclVal *pa = (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(a));
    JaclVal *pb = (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(b));
    if (!jacl_str_eq(pa[0], pb[0])) return 0;
    int32_t n = jaclrt_as_i32(pa[1]);
    if (n != jaclrt_as_i32(pb[1])) return 0;
    for (int32_t k = 0; k < n; k++) {
      if (!jacl_str_eq(pa[2 + 2 * k], pb[2 + 2 * k])) return 0;
      if (!jacl_val_equal(pa[3 + 2 * k], pb[3 + 2 * k])) return 0;
    }
    return 1;
  }
  if (t == 0x07) {                                       /* MAP */
    uint32_t n = jacl_map_count(a);
    if (n != jacl_map_count(b)) return 0;
    if (n > JACL_EQ_MAP_CAP) return 0;                   /* over cap: conservative not-equal */
    JaclVal ks[JACL_EQ_MAP_CAP], vs[JACL_EQ_MAP_CAP];
    uint32_t m = jacl_map_entries(a, ks, vs, JACL_EQ_MAP_CAP);
    for (uint32_t i = 0; i < m; i++) {
      if (!jacl_map_has(b, ks[i])) return 0;
      if (!jacl_val_equal(vs[i], jacl_map_get(b, ks[i]))) return 0;
    }
    return 1;
  }
  return 0;
}
JaclVal jacl_eq(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  return jaclrt_bool(jacl_val_equal(a, b) != 0) | prop_flags(a, b);
}
JaclVal jacl_ne(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  return jaclrt_bool(jacl_val_equal(a, b) == 0) | prop_flags(a, b);
}
JaclVal jacl_lt(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (jaclrt_is_i32(a) && jaclrt_is_i32(b))
    return jaclrt_bool(jaclrt_as_i32(a) < jaclrt_as_i32(b)) | prop_flags(a, b);
  if (jacl_is_anyint(a) && jacl_is_anyint(b) && (jacl_is_iwide(a) || jacl_is_iwide(b)))
    return jaclrt_bool(jacl_int_val(a) < jacl_int_val(b)) | prop_flags(a, b);
  if ((jacl_is_anyfloat(a) || jacl_is_anyint(a)) && (jacl_is_anyfloat(b) || jacl_is_anyint(b)))
    return jaclrt_bool(jacl_num_f64(a) < jacl_num_f64(b)) | prop_flags(a, b);
  return jaclrt_error();
}
JaclVal jacl_le(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (jaclrt_is_i32(a) && jaclrt_is_i32(b))
    return jaclrt_bool(jaclrt_as_i32(a) <= jaclrt_as_i32(b)) | prop_flags(a, b);
  if (jacl_is_anyint(a) && jacl_is_anyint(b) && (jacl_is_iwide(a) || jacl_is_iwide(b)))
    return jaclrt_bool(jacl_int_val(a) <= jacl_int_val(b)) | prop_flags(a, b);
  if ((jacl_is_anyfloat(a) || jacl_is_anyint(a)) && (jacl_is_anyfloat(b) || jacl_is_anyint(b)))
    return jaclrt_bool(jacl_num_f64(a) <= jacl_num_f64(b)) | prop_flags(a, b);
  return jaclrt_error();
}
JaclVal jacl_gt(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (jaclrt_is_i32(a) && jaclrt_is_i32(b))
    return jaclrt_bool(jaclrt_as_i32(a) > jaclrt_as_i32(b)) | prop_flags(a, b);
  if (jacl_is_anyint(a) && jacl_is_anyint(b) && (jacl_is_iwide(a) || jacl_is_iwide(b)))
    return jaclrt_bool(jacl_int_val(a) > jacl_int_val(b)) | prop_flags(a, b);
  if ((jacl_is_anyfloat(a) || jacl_is_anyint(a)) && (jacl_is_anyfloat(b) || jacl_is_anyint(b)))
    return jaclrt_bool(jacl_num_f64(a) > jacl_num_f64(b)) | prop_flags(a, b);
  return jaclrt_error();
}
JaclVal jacl_ge(JaclVal a, JaclVal b) {
  ERR_IF_ERR(a, b);
  if (jaclrt_is_i32(a) && jaclrt_is_i32(b))
    return jaclrt_bool(jaclrt_as_i32(a) >= jaclrt_as_i32(b)) | prop_flags(a, b);
  if (jacl_is_anyint(a) && jacl_is_anyint(b) && (jacl_is_iwide(a) || jacl_is_iwide(b)))
    return jaclrt_bool(jacl_int_val(a) >= jacl_int_val(b)) | prop_flags(a, b);
  if ((jacl_is_anyfloat(a) || jacl_is_anyint(a)) && (jacl_is_anyfloat(b) || jacl_is_anyint(b)))
    return jaclrt_bool(jacl_num_f64(a) >= jacl_num_f64(b)) | prop_flags(a, b);
  return jaclrt_error();
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
  if (t == 0x06 || t == 0x1B) return jaclrt_i32((int32_t)jacl_vec_count(v));  /* VECTOR / typed vec */
  if (t == 0x07) return jaclrt_i32((int32_t)jacl_map_count(v));   /* MAP    */
  if (t == 0x1A) return jaclrt_i32((int32_t)jacl_arr_count(v));   /* ARR    */
  return jaclrt_error();
}
/* JaclVal-uniform wrappers over ops whose native signatures take/return raw ints —
 * the codegen's builtin-dispatch ABI is (JaclVal args…) -> JaclVal throughout. */
JaclVal jacl_vec_get_at(JaclVal v, JaclVal idx) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_type_index(idx) != 0x02) return jaclrt_error();      /* index must be i32 */
  int32_t i = jaclrt_as_i32(idx);
  if (i < 0) return jaclrt_error();
  return jacl_vec_get(v, (uint32_t)i);
}
JaclVal jacl_map_has_v(JaclVal m, JaclVal k) {
  if (jaclrt_is_error(m)) return m;
  return jaclrt_bool(jacl_map_has(m, k) != 0);
}
JaclVal jacl_is_error_v(JaclVal v) { return jaclrt_bool(jaclrt_is_error(v)); }
JaclVal jacl_error_new(JaclVal v) { return jaclrt_set_error(v); }          /* [error V] */
JaclVal jacl_error_val(JaclVal v) { return v & ~JACL_FLAG_ERROR; }         /* payload, flag cleared */

/* ---- structs (dynamic, name-based; typed/unboxed lowering is a later pass) ----
 * A struct instance is a traced heap cell (JACL_TAG_STRUCT over JOBJ_NODE):
 *   payload JaclVals: [0] type name (string), [1] field count (i32),
 *                     [2 + 2k] field-k name (string), [3 + 2k] field-k value.
 * All slots are JaclVals, so conservative payload tracing keeps fields alive. */
JaclVal jacl_struct_new(JaclVal typename_, JaclVal nfields) {
  int32_t n = jaclrt_is_i32(nfields) ? jaclrt_as_i32(nfields) : 0;
  if (n < 0) n = 0;
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_NODE, (uint32_t)((2 + 2 * n) * 8));
  JaclVal *p = (JaclVal *)jacl_obj_payload(o);
  p[0] = typename_;
  p[1] = jaclrt_i32(n);
  for (int32_t k = 0; k < n; k++) { p[2 + 2 * k] = JACL_NIL; p[3 + 2 * k] = JACL_NIL; }
  return jaclrt_from_ptr(JACL_TAG_STRUCT, o);
}
/* Initialize field slot `idx` (construction): sets name + value in place, returns s. */
JaclVal jacl_struct_init_field(JaclVal s, JaclVal idx, JaclVal name, JaclVal value) {
  if (jaclrt_is_error(s)) return s;
  JaclVal *p = (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(s));
  int32_t n = jaclrt_as_i32(p[1]), i = jaclrt_is_i32(idx) ? jaclrt_as_i32(idx) : -1;
  if (i < 0 || i >= n) return jaclrt_error();
  p[2 + 2 * i] = name;
  p[3 + 2 * i] = value;
  return s;
}
static JaclVal *struct_field_slot(JaclVal s, JaclVal name) {
  if (jaclrt_type_index(s) != 0x12) return 0;
  JaclVal *p = (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(s));
  int32_t n = jaclrt_as_i32(p[1]);
  for (int32_t k = 0; k < n; k++)
    if (jacl_str_eq(p[2 + 2 * k], name)) return &p[3 + 2 * k];
  return 0;
}
JaclVal jacl_struct_get(JaclVal s, JaclVal name) {
  if (jaclrt_is_error(s)) return s;
  if (jaclrt_is_error(name)) return name;
  JaclVal *slot = struct_field_slot(s, name);
  return slot ? *slot : jaclrt_error();
}
JaclVal jacl_struct_put(JaclVal s, JaclVal name, JaclVal v) {   /* in-place field mutation */
  if (jaclrt_is_error(s)) return s;
  if (jaclrt_is_error(name)) return name;
  if (jaclrt_is_error(v)) return v;
  JaclVal *slot = struct_field_slot(s, name);
  if (!slot) return jaclrt_error();
  *slot = v;
  return v;
}

/* ---- box / deref / reset — a mutable single-slot ref (over the cell object) ---- */
JaclVal jacl_box_new(JaclVal v) {
  JaclVal c = jacl_cell_new(v);
  return jaclrt_from_ptr(JACL_TAG_BOX, jaclrt_as_ptr(c));
}
JaclVal jacl_box_get(JaclVal b) {
  if (jaclrt_is_error(b)) return b;
  uint32_t t = jaclrt_type_index(b);
  if (t != 0x0B && t != 0x0A && t != 0x0C) return jaclrt_error();  /* box / cell / atom */
  return jacl_cell_get(jaclrt_from_ptr(JACL_TAG_CELL, jaclrt_as_ptr(b)));
}
JaclVal jacl_box_set(JaclVal b, JaclVal v) {
  if (jaclrt_is_error(b)) return b;
  uint32_t t = jaclrt_type_index(b);
  if (t != 0x0B && t != 0x0A && t != 0x0C) return jaclrt_error();
  (void)jacl_cell_set(jaclrt_from_ptr(JACL_TAG_CELL, jaclrt_as_ptr(b)), v);
  return v;
}
JaclVal jacl_is_box_v(JaclVal v) { return jaclrt_bool(jaclrt_type_index(v) == 0x0B); }
/* [sleep S] — S seconds (i32 or f32): park on a private futex word until the
 * timeout elapses (the futex wait re-checks GC, so a sleeping fiber's worker
 * still answers stop-the-world requests through the scheduler loop). */
int __vm_wait32(void *p, int expected, long timeout_ns);
static int32_t jacl_sleep_word;
JaclVal jacl_sleep(JaclVal secs) {
  if (jaclrt_is_error(secs)) return secs;
  double s = jaclrt_is_i32(secs) ? (double)jaclrt_as_i32(secs)
           : (jaclrt_type_index(secs) == 0x03 ? (double)jaclrt_as_f32(secs) : -1.0);
  if (s < 0) return jaclrt_error();
  long total = (long)(s * 1e9);
  while (total > 0) {
    long chunk = total > 1000000L ? 1000000L : total;   /* 1 ms slices: stay GC-responsive */
    (void)__vm_wait32(&jacl_sleep_word, 0, chunk);
    total -= chunk;
  }
  return JACL_NIL;
}
/* Atoms: a mutable ref like box, tagged JACL_TAG_ATOM (deref/reset accept both). */
JaclVal jacl_atom_new(JaclVal v) {
  JaclVal c = jacl_cell_new(v);
  return jaclrt_from_ptr(JACL_TAG_ATOM, jaclrt_as_ptr(c));
}
JaclVal jacl_is_atom_v(JaclVal v) { return jaclrt_bool(jaclrt_type_index(v) == 0x0C); }
/* `[future? V]` — a spawn/parallel/race future is a job carried under the STREAM tag. */
JaclVal jacl_is_future_v(JaclVal v) { return jaclrt_bool(jaclrt_type_index(v) == 0x15); }
/* Is V a map? (0x07) — the runtime dispatch for filter/transform over a map vs a vec. */
JaclVal jacl_is_map_v(JaclVal v) { return jaclrt_bool(jaclrt_type_index(v) == 0x07); }

/* ---- ambient context (`\$ctx`) — a persistent map in a runtime global ----
 * The global holds a heap value, so it is reported as a GC root from
 * jacl_sched_mark_roots (guest-memory roots are the guest's to report). */
JaclVal jacl_ctx_cur;   /* nil until first use (nil -> empty map lazily) */
JaclVal jacl_ctx_get(void) {
  if (jaclrt_is_nil(jacl_ctx_cur)) jacl_ctx_cur = jacl_map_empty();
  return jacl_ctx_cur;
}
JaclVal jacl_ctx_swap(JaclVal m) {
  JaclVal old = jacl_ctx_get();
  jacl_ctx_cur = m;
  return old;
}
JaclVal jacl_ctx_set_field(JaclVal name, JaclVal v) {
  jacl_ctx_cur = jacl_map_set(jacl_ctx_get(), name, v);
  return v;
}

/* ---- module globals — top-level def/mut names visible from any proc/closure ----
 * A persistent map keyed by name; like $ctx, the global holds a heap value so it is a GC
 * root (jacl_globals_mark_root, called from jacl_sched_mark_roots). */
JaclVal jacl_module_globals;   /* nil -> empty map lazily */
static JaclVal jacl_globals_map(void) {
  if (jaclrt_is_nil(jacl_module_globals)) jacl_module_globals = jacl_map_empty();
  return jacl_module_globals;
}
JaclVal jacl_global_get(JaclVal name) { return jacl_map_get(jacl_globals_map(), name); }
JaclVal jacl_global_set(JaclVal name, JaclVal v) {
  jacl_module_globals = jacl_map_set(jacl_globals_map(), name, v);
  return v;
}

/* Widen a value to a declared wide scalar kind (typed defs): 14=i64, 15=u64, 16=f64. */
JaclVal jacl_widen_to(JaclVal v, JaclVal kind) {
  if (jaclrt_is_error(v) || !jaclrt_is_i32(kind)) return v;
  int32_t k = jaclrt_as_i32(kind);
  if (k == 0x10) return jacl_is_f64(v) ? v : jacl_f64_new(jacl_num_f64(v));
  if (k == 0x0E || k == 0x0F) {
    if (jacl_is_iwide(v)) return v;
    if (jaclrt_is_i32(v)) return jacl_wide_new((uint32_t)k, (int64_t)jaclrt_as_i32(v));
  }
  return v;
}
/* [to TYPE V] — numeric/string casts (subset of the old VM's `to`). */
JaclVal jacl_to_cast(JaclVal v, JaclVal tname) {
  if (jaclrt_is_error(v)) return v;
  char tn[16];
  uint32_t tl = jacl_str_len(tname);
  if (tl > sizeof tn - 1) tl = sizeof tn - 1;
  jacl_str_bytes(tname, tn, sizeof tn);
  if (tl == 3 && !memcmp(tn, "i64", 3)) return jacl_wide_new(0x0E, jacl_is_anyfloat(v) ? (int64_t)jacl_num_f64(v) : jacl_int_val(v));
  if (tl == 3 && !memcmp(tn, "u64", 3)) return jacl_wide_new(0x0F, jacl_is_anyfloat(v) ? (int64_t)jacl_num_f64(v) : jacl_int_val(v));
  if (tl == 3 && !memcmp(tn, "f64", 3)) return jacl_f64_new(jacl_num_f64(v));
  if (tl == 3 && !memcmp(tn, "f32", 3)) return jaclrt_f32v((float)jacl_num_f64(v));
  if (tl == 3 && !memcmp(tn, "i32", 3)) {
    if (jaclrt_is_i32(v)) return v;
    if (jacl_is_anyint(v)) return jaclrt_i32((int32_t)jacl_int_val(v));
    if (jacl_is_anyfloat(v)) return jaclrt_i32((int32_t)jacl_num_f64(v));
    return jaclrt_error();
  }
  if (tl == 3 && !memcmp(tn, "str", 3)) return jacl_to_string(v);
  return jaclrt_error();
}

/* [lines S] — split a string on newlines into a vector of strings. */
JaclVal jacl_lines(JaclVal s) {
  if (jaclrt_is_error(s)) return s;
  if (!jaclrt_is_string(s)) return jaclrt_error();
  char buf[2048];
  uint32_t len = jacl_str_len(s);
  if (len > sizeof buf - 1) len = sizeof buf - 1;
  jacl_str_bytes(s, buf, sizeof buf);
  JaclVal out = jacl_vec_empty();
  uint32_t start = 0;
  for (uint32_t i = 0; i <= len; i++) {
    if (i == len || buf[i] == '\n') {
      uint32_t end = i;
      if (end > start && buf[end - 1] == '\r') end--;   /* strip a trailing CR (CRLF) */
      out = jacl_vec_push(out, jacl_str_new(buf + start, end - start));
      start = i + 1;
    }
  }
  return out;
}

/* [assert COND] — nil when truthy, an error otherwise. */
JaclVal jacl_assert(JaclVal v) {
  if (jaclrt_is_error(v)) return v;
  int truthy = !(v == JACL_FALSE || v == JACL_NIL);
  return truthy ? JACL_NIL : jaclrt_error();
}
/* [range A B] as a value: the vector [A, B) of i32s (for `for`/transform sources). */
/* Re-tag a vector as a TYPED vector (`[Vec T]`) — same root cell, distinct tag so it prints
 * comma-style. A non-vector passes through unchanged. */
JaclVal jacl_tvec_mark(JaclVal v) {
  if (jaclrt_type_index(v) != 0x06) return v;
  return jaclrt_from_ptr(JACL_TAG_TVEC, jaclrt_as_ptr(v));
}
JaclVal jacl_range_vec(JaclVal a, JaclVal b) {
  if (jaclrt_is_error(a)) return a;
  if (jaclrt_is_error(b)) return b;
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  int32_t lo = jaclrt_as_i32(a), hi = jaclrt_as_i32(b);
  JaclVal out = jacl_vec_empty();
  for (int32_t i = lo; i < hi; i++) out = jacl_vec_push(out, jaclrt_i32(i));
  return jacl_tvec_mark(out);   /* range produces a typed [Vec i64] */
}
/* [assert-type V T] — dynamic type assertion: V when it matches the named type,
 * an error otherwise. (The old compiler proves many of these statically; the
 * dynamic check preserves the passing corpus behavior.) */
static int type_name_matches(JaclVal v, const char *tn, uint32_t tl) {
  uint32_t t = jaclrt_type_index(v);
  if (tl == 3 && !memcmp(tn, "str", 3)) return t == 0x04 || t == 0x05 || t == 0x14;
  if (tl == 3 && !memcmp(tn, "i32", 3)) return t == 0x02;
  if (tl == 3 && !memcmp(tn, "f32", 3)) return t == 0x03;
  if (tl == 3 && !memcmp(tn, "f64", 3)) return t == 0x03 || t == 0x10;
  if (tl == 3 && !memcmp(tn, "i64", 3)) return t == 0x02 || t == 0x0E;
  if (tl == 3 && !memcmp(tn, "u64", 3)) return t == 0x02 || t == 0x0F;
  if (tl == 3 && !memcmp(tn, "vec", 3)) return t == 0x06;
  if (tl == 3 && !memcmp(tn, "map", 3)) return t == 0x07;
  if (tl == 3 && !memcmp(tn, "arr", 3)) return t == 0x1A;
  if (tl == 3 && !memcmp(tn, "dyn", 3)) return 1;
  if (tl == 4 && !memcmp(tn, "bool", 4)) return t == 0x01;
  if (tl == 3 && !memcmp(tn, "nil", 3)) return t == 0x00;
  return 1;   /* unknown names (struct types, compounds): accept */
}
JaclVal jacl_assert_type(JaclVal v, JaclVal tname) {
  if (jaclrt_is_error(v)) return v;
  char tn[32];
  uint32_t tl = jacl_str_len(tname);
  if (tl > sizeof tn - 1) tl = sizeof tn - 1;
  jacl_str_bytes(tname, tn, sizeof tn);
  return type_name_matches(v, tn, tl) ? v : jaclrt_error();
}

/* `[?. V KEY]` — optional chaining: nil short-circuits to nil, a map reads the entry
 * (missing key -> nil), and any other indexable/record value falls through to the
 * dynamic dot. (Structs are a compile error in the old VM — `use -> instead`; here a
 * struct value yields an error at runtime.) */
JaclVal jacl_qdot(JaclVal v, JaclVal key) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_is_nil(v)) return jaclrt_nil();
  uint32_t t = jaclrt_type_index(v);
  if (t == 0x07) return jacl_map_get(v, key);   /* map: value or nil */
  if (t == 0x12) return jaclrt_error();          /* struct: -> is required */
  return jacl_dot_dyn(v, key);
}

/* Dynamic dot: `\$v->\$k` — an i32 key indexes (buf/arr/vec), a string key reads a
 * field/entry (struct/map). */
JaclVal jacl_dot_dyn(JaclVal v, JaclVal k) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_is_error(k)) return k;
  if (jaclrt_is_i32(k)) return jacl_index_get(v, k);
  return jacl_field_get(v, k);
}
JaclVal jacl_dot_dyn_set(JaclVal v, JaclVal k, JaclVal nv) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_is_error(k)) return k;
  if (jaclrt_is_error(nv)) return nv;
  if (jaclrt_is_i32(k)) return jacl_arr_set_at(v, k, nv);
  uint32_t t = jaclrt_type_index(v);
  if (t == 0x12) return jacl_struct_put(v, k, nv);
  return jaclrt_error();
}
/* Destructuring accessor: a named `{a, b}` pattern binds by FIELD on a struct/map but
 * POSITIONALLY (by index) on a vector/array, so the codegen passes both the field name
 * and the pattern position and the runtime picks by the value's type. */
JaclVal jacl_field_or_index(JaclVal v, JaclVal name, JaclVal idx) {
  if (jaclrt_is_error(v)) return v;
  uint32_t t = jaclrt_type_index(v);
  if (t == 0x06 || t == 0x1A) return jacl_index_get(v, idx);   /* vector / array */
  return jacl_field_get(v, name);                              /* struct / map */
}
/* Named-field access across record-like values: struct field or map entry (string key). */
JaclVal jacl_field_get(JaclVal v, JaclVal name) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_is_error(name)) return name;
  uint32_t t = jaclrt_type_index(v);
  if (t == 0x12) return jacl_struct_get(v, name);
  if (t == 0x07) return jacl_map_get(v, name);
  return jaclrt_error();
}
JaclVal jacl_vec_set_at(JaclVal v, JaclVal idx, JaclVal elem) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_type_index(idx) != 0x02) return jaclrt_error();
  int32_t i = jaclrt_as_i32(idx);
  if (i < 0) return jaclrt_error();
  return jacl_vec_set(v, (uint32_t)i, elem);
}
/* `[vec-push V E]` — error-propagating push wrapper (the core jacl_vec_push is also
 * used to build literals / accumulate results, where inputs are never errors, so the
 * propagation lives here on the builtin edge). */
JaclVal jacl_vec_push_v(JaclVal v, JaclVal elem) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_is_error(elem)) return elem;
  uint32_t tv = jaclrt_type_index(v);
  if (tv != 0x06 && tv != 0x1B) return jaclrt_error();   /* dynamic or typed vec */
  return jacl_vec_push(v, elem);
}
/* `[vec-slice V START END]` — a fresh vector of V[START..END) (half-open). START/END
 * are clamped to [0, len]; START past END yields an empty vector. */
JaclVal jacl_vec_slice(JaclVal v, JaclVal start, JaclVal end) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_is_error(start)) return start;
  if (jaclrt_is_error(end)) return end;
  { uint32_t tv = jaclrt_type_index(v);
    if ((tv != 0x06 && tv != 0x1B) || !jaclrt_is_i32(start) || !jaclrt_is_i32(end))
      return jaclrt_error(); }                             /* dynamic or typed vec */
  int32_t n = (int32_t)jacl_vec_count(v);
  int32_t s = jaclrt_as_i32(start), e = jaclrt_as_i32(end);
  if (s < 0) s = 0;
  if (e > n) e = n;
  JaclVal out = jacl_vec_empty();
  for (int32_t i = s; i < e; i++) out = jacl_vec_push(out, jacl_vec_get(v, (uint32_t)i));
  return out;
}
/* `[index X i]` — a string yields its i-th byte as a 1-char string; a vector/arr
 * yields element i. `[slice X a b]` — a string yields the substring, a vector the
 * sub-vector. Dispatched on X's runtime type so one head serves both. */
JaclVal jacl_index_op(JaclVal x, JaclVal idx) {
  if (jaclrt_is_error(x)) return x;
  if (jaclrt_is_string(x)) return jacl_str_index(x, idx);
  return jacl_index_get(x, idx);
}
JaclVal jacl_slice_op(JaclVal x, JaclVal a, JaclVal b) {
  if (jaclrt_is_error(x)) return x;
  if (jaclrt_is_string(x)) return jacl_str_slice(x, a, b);
  return jacl_vec_slice(x, a, b);
}
/* `[arr-push A E]` — push and return the NEW length (the old VM's arr-push value),
 * unlike the core jacl_arr_push which returns the array for internal chaining. */
JaclVal jacl_arr_push_v(JaclVal a, JaclVal e) {
  if (jaclrt_is_error(a)) return a;
  if (jaclrt_is_error(e)) return e;
  JaclVal r = jacl_arr_push(a, e);
  if (jaclrt_is_error(r)) return r;
  return jaclrt_i32((int32_t)jacl_arr_count(a));
}
/* ---- buffer pointers (fat pointer over an array-backed buffer) ----
 * `addr $buf->i` yields a pointer = a small 2-slot object { base, offset }; ptr-deref
 * reads base[offset]; ptr-offset advances the offset. On the SVM backend buffers are
 * JaclVal arrays (not raw memory), so a pointer carries (array, index) rather than a
 * machine address — enough to walk a buffer's elements the way a C extern would. The
 * object is a 2-element vector internally (traced, so the base survives GC). */
JaclVal jacl_addr_of(JaclVal base, JaclVal idx) {
  if (jaclrt_is_error(base)) return base;
  if (jaclrt_is_error(idx)) return idx;
  JaclVal p = jacl_vec_empty();
  p = jacl_vec_push(p, base);
  p = jacl_vec_push(p, jaclrt_is_i32(idx) ? idx : jaclrt_i32(0));
  return p;
}
JaclVal jacl_ptr_deref(JaclVal p) {
  if (jaclrt_is_error(p)) return p;
  /* A buffer that decayed to a pointer at a proc-param boundary arrives as the bare
   * array (0x1A) — deref reads element 0 (C-style decay: the pointer is `&buf[0]`). */
  if (jaclrt_type_index(p) == 0x1A) return jacl_arr_get(p, 0);
  if (jaclrt_type_index(p) != 0x06) return jaclrt_error();
  return jacl_index_get(jacl_vec_get(p, 0), jacl_vec_get(p, 1));
}
JaclVal jacl_ptr_offset(JaclVal p, JaclVal n) {
  if (jaclrt_is_error(p)) return p;
  if (jaclrt_is_error(n)) return n;
  if (!jaclrt_is_i32(n)) return jaclrt_error();
  JaclVal base, off;
  if (jaclrt_type_index(p) == 0x1A) { base = p; off = jaclrt_i32(0); }   /* decayed buffer */
  else if (jaclrt_type_index(p) == 0x06) { base = jacl_vec_get(p, 0); off = jacl_vec_get(p, 1); }
  else return jaclrt_error();
  JaclVal noff = jaclrt_i32(jaclrt_as_i32(off) + jaclrt_as_i32(n));
  JaclVal np = jacl_vec_empty();
  np = jacl_vec_push(np, base);
  np = jacl_vec_push(np, noff);
  return np;
}
/* `[first C]` — the first element of a vector/arr/map (nil-safe via iter accessor). */
JaclVal jacl_first(JaclVal c) {
  if (jaclrt_is_error(c)) return c;
  return jacl_iter_val_at(c, jaclrt_i32(0));
}
/* Left-fold an arithmetic operator over a collection's elements — the runtime side of
 * spreading a vector into a variadic operator call (`[+ ..$v]` -> reduce). `opid`
 * selects add/sub/mul/div/mod; an empty collection yields the additive identity for +
 * (0) and an error otherwise (matching a nullary operator). */
JaclVal jacl_vec_reduce(JaclVal c, JaclVal opid) {
  if (jaclrt_is_error(c)) return c;
  if (!jaclrt_is_i32(opid)) return jaclrt_error();
  int32_t op = jaclrt_as_i32(opid);
  uint32_t t = jaclrt_type_index(c);
  JaclVal acc = 0; int have = 0;
  if (t == 0x15) {                                       /* generator/stream: drain + fold */
    while (1) {
      JaclVal e = jacl_gen_next(c);
      JaclVal d = jacl_gen_done(c);
      if (jaclrt_is_bool(d) && jaclrt_as_bool(d)) break; /* done: e is the return, not a value */
      if (!have) { acc = e; have = 1; continue; }
      switch (op) {
        case 0: acc = jacl_add(acc, e); break;
        case 1: acc = jacl_sub(acc, e); break;
        case 2: acc = jacl_mul(acc, e); break;
        case 3: acc = jacl_div(acc, e); break;
        case 4: acc = jacl_mod(acc, e); break;
        default: return jaclrt_error();
      }
      if (jaclrt_is_error(acc)) return acc;
    }
    return have ? acc : (op == 0 ? jaclrt_i32(0) : jaclrt_error());
  }
  if (t != 0x06 && t != 0x1A) return jaclrt_error();     /* else vectors / arrays only */
  uint32_t n = t == 0x1A ? jacl_arr_count(c) : jacl_vec_count(c);
  if (n == 0) return op == 0 ? jaclrt_i32(0) : jaclrt_error();
  acc = jacl_iter_val_at(c, jaclrt_i32(0));
  for (uint32_t i = 1; i < n; i++) {
    JaclVal e = jacl_iter_val_at(c, jaclrt_i32((int32_t)i));
    switch (op) {
      case 0: acc = jacl_add(acc, e); break;
      case 1: acc = jacl_sub(acc, e); break;
      case 2: acc = jacl_mul(acc, e); break;
      case 3: acc = jacl_div(acc, e); break;
      case 4: acc = jacl_mod(acc, e); break;
      default: return jaclrt_error();
    }
    if (jaclrt_is_error(acc)) return acc;
  }
  return acc;
}
/* for-over-collection accessors: the i-th VALUE and KEY of any iterable. A vector/arr
 * yields (index -> element) with the index itself as the key; a map yields its i-th
 * (key, value) entry in deterministic HAMT order (stable across the two calls, so a
 * two-name `for $m k v` binds a consistent pair). */
JaclVal jacl_iter_val_at(JaclVal c, JaclVal idx) {
  if (jaclrt_is_error(c)) return c;
  if (!jaclrt_is_i32(idx)) return jaclrt_error();
  int32_t i = jaclrt_as_i32(idx);
  if (jaclrt_type_index(c) == 0x07) {                    /* MAP: i-th value */
    JaclVal ks[JACL_EQ_MAP_CAP], vs[JACL_EQ_MAP_CAP];
    uint32_t n = jacl_map_entries(c, ks, vs, JACL_EQ_MAP_CAP);
    if (i < 0 || (uint32_t)i >= n) return jaclrt_error();
    return vs[i];
  }
  return jacl_index_get(c, idx);                          /* vec / arr element */
}
JaclVal jacl_iter_key_at(JaclVal c, JaclVal idx) {
  if (jaclrt_is_error(c)) return c;
  if (!jaclrt_is_i32(idx)) return jaclrt_error();
  int32_t i = jaclrt_as_i32(idx);
  if (jaclrt_type_index(c) == 0x07) {                    /* MAP: i-th key */
    JaclVal ks[JACL_EQ_MAP_CAP], vs[JACL_EQ_MAP_CAP];
    uint32_t n = jacl_map_entries(c, ks, vs, JACL_EQ_MAP_CAP);
    if (i < 0 || (uint32_t)i >= n) return jaclrt_error();
    return ks[i];
  }
  return idx;                                             /* vec / arr: key is the index */
}
/* `[range-inclusive A B]` — [A, B] as a vector (the closed-interval sibling of range). */
JaclVal jacl_range_inclusive(JaclVal a, JaclVal b) {
  if (jaclrt_is_error(a)) return a;
  if (jaclrt_is_error(b)) return b;
  if (!jaclrt_is_i32(a) || !jaclrt_is_i32(b)) return jaclrt_error();
  int32_t lo = jaclrt_as_i32(a), hi = jaclrt_as_i32(b);
  JaclVal out = jacl_vec_empty();
  for (int32_t i = lo; i <= hi; i++) out = jacl_vec_push(out, jaclrt_i32(i));
  return jacl_tvec_mark(out);   /* range-inclusive produces a typed [Vec i64] */
}
JaclVal jacl_map_keys_v(JaclVal m) {
  if (jaclrt_is_error(m)) return m;
  JaclVal ks[JACL_EQ_MAP_CAP], vs[JACL_EQ_MAP_CAP];
  uint32_t n = jacl_map_entries(m, ks, vs, JACL_EQ_MAP_CAP);
  JaclVal out = jacl_vec_empty();
  for (uint32_t i = 0; i < n; i++) out = jacl_vec_push(out, ks[i]);
  return out;
}
JaclVal jacl_map_vals_v(JaclVal m) {
  if (jaclrt_is_error(m)) return m;
  JaclVal ks[JACL_EQ_MAP_CAP], vs[JACL_EQ_MAP_CAP];
  uint32_t n = jacl_map_entries(m, ks, vs, JACL_EQ_MAP_CAP);
  JaclVal out = jacl_vec_empty();
  for (uint32_t i = 0; i < n; i++) out = jacl_vec_push(out, vs[i]);
  return out;
}
/* Recursive text form (constructor syntax for collections, matching the old VM's
 * print: `[vec 1 2 3]`, `[map "a" 1]` — strings QUOTED inside collections, raw at
 * top level). Renders into a bounded buffer (truncates past cap; corpus-scale
 * values are small). */
typedef struct { char *p; uint32_t n, cap; } JaclRepr;
static void repr_put(JaclRepr *rb, const char *s, uint32_t n) {
  for (uint32_t i = 0; i < n && rb->n < rb->cap; i++) rb->p[rb->n++] = s[i];
}
/* Format an f32 like the old VM's display: up to 6 fractional digits, trailing
 * zeros stripped; integral values print without a decimal point. */
static void repr_f32(JaclRepr *rb, float fv) {
  double v = (double)fv;
  if (v != v) { repr_put(rb, "nan", 3); return; }
  if (v < 0) { repr_put(rb, "-", 1); v = -v; }
  if (v > 2147483000.0) { repr_put(rb, "inf", 3); return; }
  uint64_t scaled = (uint64_t)(v * 1000000.0 + 0.5);
  uint64_t ip = scaled / 1000000u, fp = scaled % 1000000u;
  char tmp[24]; int j = 0;
  if (ip == 0) tmp[j++] = '0';
  while (ip) { tmp[j++] = (char)('0' + (ip % 10)); ip /= 10; }
  while (j) { j--; repr_put(rb, &tmp[j], 1); }
  if (fp) {
    char fd[6];
    for (int k = 5; k >= 0; k--) { fd[k] = (char)('0' + (fp % 10)); fp /= 10; }
    int last = 5;
    while (last >= 0 && fd[last] == '0') last--;
    repr_put(rb, ".", 1);
    for (int k = 0; k <= last; k++) repr_put(rb, &fd[k], 1);
  }
}
static void repr_i32(JaclRepr *rb, int32_t x) {
  char tmp[16]; int j = 0;
  int neg = x < 0;
  uint32_t u = neg ? (uint32_t)(-(int64_t)x) : (uint32_t)x;
  if (u == 0) tmp[j++] = '0';
  while (u) { tmp[j++] = (char)('0' + (u % 10)); u /= 10; }
  if (neg) repr_put(rb, "-", 1);
  while (j) { j--; repr_put(rb, &tmp[j], 1); }
}
static void repr_val(JaclRepr *rb, JaclVal v, int quote_strings) {
  if (jaclrt_is_string(v)) {
    char sb[1024];
    uint32_t len = jacl_str_len(v);
    if (len > sizeof sb - 1) len = sizeof sb - 1;
    jacl_str_bytes(v, sb, sizeof sb);
    if (quote_strings) repr_put(rb, "\"", 1);
    repr_put(rb, sb, len);
    if (quote_strings) repr_put(rb, "\"", 1);
    return;
  }
  uint32_t t = jaclrt_type_index(v);
  if (t == 0x00) { repr_put(rb, "nil", 3); return; }
  if (t == 0x01) { if (jaclrt_as_bool(v)) repr_put(rb, "true", 4); else repr_put(rb, "false", 5); return; }
  if (t == 0x02) { repr_i32(rb, jaclrt_as_i32(v)); return; }
  if (t == 0x03) { repr_f32(rb, jaclrt_as_f32(v)); return; }
  if (t == 0x0E || t == 0x0F) {                          /* wide ints: 64-bit itoa */
    int64_t x = jacl_wide_bits(v);
    char tmp[24]; int j = 0;
    int neg = (t == 0x0E && x < 0);
    uint64_t u = neg ? (uint64_t)(-x) : (uint64_t)x;
    if (u == 0) tmp[j++] = '0';
    while (u) { tmp[j++] = (char)('0' + (u % 10)); u /= 10; }
    if (neg) repr_put(rb, "-", 1);
    while (j) { j--; repr_put(rb, &tmp[j], 1); }
    return;
  }
  if (t == 0x10) { repr_f32(rb, (float)jacl_num_f64(v)); return; }   /* f64: shared formatter */
  if (t == 0x06) {                                       /* VECTOR: [vec e0 e1 …] */
    repr_put(rb, "[vec", 4);
    uint32_t n = jacl_vec_count(v);
    for (uint32_t i = 0; i < n; i++) {
      repr_put(rb, " ", 1);
      repr_val(rb, jacl_vec_get(v, i), 1);
    }
    repr_put(rb, "]", 1);
    return;
  }
  if (t == 0x1B) {                                       /* TYPED VECTOR: [e0, e1, …] */
    repr_put(rb, "[", 1);
    uint32_t n = jacl_vec_count(v);
    for (uint32_t i = 0; i < n; i++) {
      if (i) repr_put(rb, ", ", 2);
      repr_val(rb, jacl_vec_get(v, i), 1);
    }
    repr_put(rb, "]", 1);
    return;
  }
  if (t == 0x07) {                                       /* MAP: [map k0 v0 …] */
    repr_put(rb, "[map", 4);
    JaclVal ks[JACL_EQ_MAP_CAP], vs[JACL_EQ_MAP_CAP];
    uint32_t m = jacl_map_entries(v, ks, vs, JACL_EQ_MAP_CAP);
    for (uint32_t i = 0; i < m; i++) {
      repr_put(rb, " ", 1);
      repr_val(rb, ks[i], 1);
      repr_put(rb, " ", 1);
      repr_val(rb, vs[i], 1);
    }
    repr_put(rb, "]", 1);
    return;
  }
  if (t == 0x1A) {                                       /* ARR: [arr e0 e1 …] */
    repr_put(rb, "[arr", 4);
    uint32_t n = jacl_arr_count(v);
    for (uint32_t i = 0; i < n; i++) {
      repr_put(rb, " ", 1);
      repr_val(rb, jacl_arr_get(v, i), 1);
    }
    repr_put(rb, "]", 1);
    return;
  }
  if (t == 0x12) {                                       /* STRUCT: [Type f0 v0 …] */
    JaclVal *p = (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(v));
    repr_put(rb, "[", 1);
    repr_val(rb, p[0], 0);                               /* type name, unquoted */
    int32_t n = jaclrt_as_i32(p[1]);
    for (int32_t k = 0; k < n; k++) {
      repr_put(rb, " ", 1);
      repr_val(rb, p[2 + 2 * k], 0);                     /* field name, unquoted */
      repr_put(rb, " ", 1);
      repr_val(rb, p[3 + 2 * k], 1);
    }
    repr_put(rb, "]", 1);
    return;
  }
  if (t == 0x0B) {                                       /* BOX: <box: CONTENTS> */
    repr_put(rb, "<box: ", 6);
    repr_val(rb, jacl_box_get(v), 1);
    repr_put(rb, ">", 1);
    return;
  }
  if (t == 0x0C) {                                       /* ATOM: <atom: CONTENTS> */
    repr_put(rb, "<atom: ", 7);
    repr_val(rb, jacl_box_get(v), 1);
    repr_put(rb, ">", 1);
    return;
  }
  repr_put(rb, "?", 1);
}
JaclVal jacl_to_string(JaclVal v) {
  if (jaclrt_is_string(v)) return v;
  uint32_t t = jaclrt_type_index(v);
  if (t != 0x00 && t != 0x01 && t != 0x02 && t != 0x03 && t != 0x06 && t != 0x07 && t != 0x12 && t != 0x1A && t != 0x1B && t != 0x0E && t != 0x0F && t != 0x10 && t != 0x0B && t != 0x0C) return jaclrt_error();
  char buf[2048];
  JaclRepr rb = {buf, 0, sizeof buf};
  repr_val(&rb, v, 1);
  return jacl_str_new(buf, rb.n);
}
