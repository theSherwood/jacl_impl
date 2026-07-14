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
/* Structural equality: strings by content, vectors elementwise, maps by content
 * (order-independent; both must have the same count and every (k,v) of `a` must
 * match in `b`), everything else bitwise type+payload. Recursive, so nested
 * collections compare deeply. Also the map key-equality handler (jmap_key_eq). */
#define JACL_EQ_MAP_CAP 64
int jacl_val_equal(JaclVal a, JaclVal b) {
  uint64_t mask = JACL_TYPE_MASK | JACL_PAYLOAD_MASK;
  if (((a ^ b) & mask) == 0) return 1;
  if (jaclrt_is_string(a) && jaclrt_is_string(b)) return jacl_str_eq(a, b);
  uint32_t t = jaclrt_type_index(a);
  if (t != jaclrt_type_index(b)) return 0;
  if (t == 0x06) {                                       /* VECTOR */
    uint32_t n = jacl_vec_count(a);
    if (n != jacl_vec_count(b)) return 0;
    for (uint32_t i = 0; i < n; i++)
      if (!jacl_val_equal(jacl_vec_get(a, i), jacl_vec_get(b, i))) return 0;
    return 1;
  }
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
  JaclVal *slot = struct_field_slot(s, name);
  return slot ? *slot : jaclrt_error();
}
JaclVal jacl_struct_put(JaclVal s, JaclVal name, JaclVal v) {   /* in-place field mutation */
  if (jaclrt_is_error(s)) return s;
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
JaclVal jacl_vec_set_at(JaclVal v, JaclVal idx, JaclVal elem) {
  if (jaclrt_is_error(v)) return v;
  if (jaclrt_type_index(idx) != 0x02) return jaclrt_error();
  int32_t i = jaclrt_as_i32(idx);
  if (i < 0) return jaclrt_error();
  return jacl_vec_set(v, (uint32_t)i, elem);
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
  repr_put(rb, "?", 1);
}
JaclVal jacl_to_string(JaclVal v) {
  if (jaclrt_is_string(v)) return v;
  uint32_t t = jaclrt_type_index(v);
  if (t != 0x00 && t != 0x01 && t != 0x02 && t != 0x06 && t != 0x07 && t != 0x12) return jaclrt_error();
  char buf[2048];
  JaclRepr rb = {buf, 0, sizeof buf};
  repr_val(&rb, v, 1);
  return jacl_str_new(buf, rb.n);
}
