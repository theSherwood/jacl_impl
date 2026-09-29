/* value.c — JACL values as unir messages (theSherwood/unir#43, docs/UNIR_PIPELINES.md "One
 * codec"). Included by chan.c with the unir backend, before pipe_unir.c.
 *
 * The schema is runtime/unir/value.usc, compiled by unir-schemac into unir/jacl_value.h: a
 * recursive sum over nil, bool, the integer and float widths, text, bytes, vectors, maps and
 * errors. Its encoding is unir's canonical one (unir spec §3): the root head at 0, then one
 * 8-aligned tail segment per non-empty slot, in pre-order. The encoder below writes exactly
 * that, and nothing is decoded that unir's verifier (`unir_schema_verify`) has not accepted:
 * a message from another vat is copied into this vat's memory, verified, then decoded, so
 * the decoder only walks bytes it knows to be well formed.
 *
 * What cannot cross: closures, futures, channel ends, atoms, structs and other heap objects
 * with identity, bigints past 64 bits, and tainted or secret values (the wire has no bits for
 * the flags, so crossing would launder them, jacl #95). Encoding one is an error value. */
#include "unir/jacl_value.h"

/* How deep a message may nest: unir-schema's MAX_DEPTH. Every step from a head into a head
 * it contains counts, so a vector nests two levels per JACL level (the payload, then each
 * element), and its verifier rejects a deeper message. */
#define JV_MAX_DEPTH 128u
#define JV_HEAD 16u      /* sizeof(jv_Value) */
#define JV_ENTRY 32u     /* sizeof(jv_Entry) */

static unir_schema *jv_schema_v;
static int32_t jv_schema_lock;

/* The value schema, as the unir unit's validated schema (built once; immutable after). */
static unir_schema *jv_schema(void) {
  unir_lock(&jv_schema_lock);
  if (!jv_schema_v) jv_schema_v = unir_schema_new(jv_values_schema, sizeof jv_values_schema);
  unir_unlock(&jv_schema_lock);
  return jv_schema_v;
}

/* 1 if `frame` is the canonical encoding of a value (in memory no peer can write). */
static int jv_verify(const uint8_t *frame, uint64_t len) {
  unir_schema *s = jv_schema();
  return s && unir_schema_verify(s, 0, frame, len) == 0;
}

/* ---- encoding ---- */

typedef struct {
  uint8_t *buf;        /* NULL while sizing */
  uint64_t len;        /* the frame's length so far: where the next segment goes */
  JaclVal err;         /* why the value cannot cross, once it cannot */
} JvEnc;

static JaclVal jv_err(const char *what, JaclVal v) {
  uint32_t n = 0;
  while (what[n]) n++;
  JaclVal m = jacl_str_new(what, n);
  if (!jaclrt_is_nil(v)) m = jacl_str_concat(m, jacl_typeof(v));
  return jacl_error_new(m);
}

static void jv_put_le(JvEnc *e, uint64_t at, uint64_t x, int n) {
  if (!e->buf) return;
  for (int k = 0; k < n; k++) e->buf[at + k] = (uint8_t)(x >> (8 * k));
}

/* Places a segment of `n` bytes at the end of the frame, writes its slot at `at`, and returns
 * its offset; an empty segment is the slot (0, 0), left as zero, and has no place. */
static uint64_t jv_segment(JvEnc *e, uint64_t at, uint64_t n, uint64_t count) {
  if (n == 0) return 0;
  uint64_t off = (e->len + 7u) & ~(uint64_t)7u;
  e->len = off + n;
  if (e->len > 0xFFFFFFFFu && jaclrt_is_nil(e->err)) e->err = jv_err("value: too large to cross", JACL_NIL);
  jv_put_le(e, at, off, 4);
  jv_put_le(e, at + 4, count, 4);
  return off;
}

static void jv_bytes(JvEnc *e, uint64_t at, const uint8_t *p, uint64_t n) {
  uint64_t off = jv_segment(e, at, n, n);
  if (e->buf && n) memcpy(e->buf + off, p, n);
}

static void jv_put(JvEnc *e, JaclVal v, uint64_t at, uint32_t depth);

/* A vector's (or an error's one) elements as a list<Value> slot at `at`. */
static void jv_list(JvEnc *e, JaclVal vec, uint32_t n, uint64_t at, uint32_t depth) {
  uint64_t off = jv_segment(e, at, (uint64_t)n * JV_HEAD, n);
  for (uint32_t i = 0; i < n && jaclrt_is_nil(e->err); i++)
    jv_put(e, jacl_vec_get(vec, i), off + (uint64_t)i * JV_HEAD, depth);
}

/* The value `v` as a head at `at`, `depth` steps below the root. */
static void jv_put(JvEnc *e, JaclVal v, uint64_t at, uint32_t depth) {
  if (!jaclrt_is_nil(e->err)) return;
  if (depth > JV_MAX_DEPTH || (depth == JV_MAX_DEPTH && v != JACL_NIL)) {
    e->err = jv_err("value: nested too deeply to cross", JACL_NIL);
    return;
  }
  if (jacl_is_flagged(v)) {
    e->err = jv_err("value: a tainted or secret value cannot cross", JACL_NIL);
    return;
  }
  uint64_t u = at + 8;   /* the union, where a payload goes (depth + 1) */
  if (jaclrt_is_error(v)) {
    /* Its payload as a one-element list<Value>. */
    jv_put_le(e, at, jv_Value_Error, 1);
    jv_put(e, jacl_error_val(v), jv_segment(e, u, JV_HEAD, 1), depth + 2);
    return;
  }
  uint32_t t = jaclrt_type_index(v);
  switch (t) {
    case 0x00: return;                                                  /* nil: all zero */
    case 0x01: jv_put_le(e, at, jv_Value_Bool, 1); jv_put_le(e, u, v & 1, 1); return;
    case 0x02: jv_put_le(e, at, jv_Value_I32, 1); jv_put_le(e, u, (uint32_t)jaclrt_as_i32(v), 4); return;
    case 0x03: jv_put_le(e, at, jv_Value_F32, 1); jv_put_le(e, u, (uint32_t)v, 4); return;
    case 0x0E: jv_put_le(e, at, jv_Value_I64, 1); jv_put_le(e, u, (uint64_t)jacl_wide_bits(v), 8); return;
    case 0x10: jv_put_le(e, at, jv_Value_F64, 1); jv_put_le(e, u, (uint64_t)jacl_wide_bits(v), 8); return;
    case 0x09: {                                                        /* bigint: a u64, or none */
      uint64_t x;
      if (!jacl_big_to_u64(v, &x)) { e->err = jv_err("value: an integer past 64 bits cannot cross", JACL_NIL); return; }
      jv_put_le(e, at, jv_Value_U64, 1);
      jv_put_le(e, u, x, 8);
      return;
    }
    case 0x04: {
      char b[8];
      jaclrt_inline_get(v, b, sizeof b);
      jv_put_le(e, at, jv_Value_Text, 1);
      jv_bytes(e, u, (const uint8_t *)b, jaclrt_inline_len(v));
      return;
    }
    case 0x05:
      jv_put_le(e, at, jv_Value_Text, 1);
      jv_bytes(e, u, (const uint8_t *)str_data(v), str_obj(v)->len);
      return;
    case 0x1E:                                                          /* [Buf n u8] */
      if (fb_ndims(v) == 1 && fb_code(v) <= 1) {
        jv_put_le(e, at, jv_Value_Bytes, 1);
        jv_bytes(e, u, (const uint8_t *)fb_data(v), (uint64_t)fb_outer(v));
        return;
      }
      break;
    case 0x06: case 0x1B:                                               /* vec, [Vec T] */
      jv_put_le(e, at, jv_Value_Vec, 1);
      jv_list(e, v, jacl_vec_count(v), u, depth + 2);
      return;
    case 0x07: {                                                        /* map */
      jv_put_le(e, at, jv_Value_Map, 1);
      uint32_t n = jaclrt_as_ptr(v) ? jacl_map_count(v) : 0;
      uint64_t off = jv_segment(e, u, (uint64_t)n * JV_ENTRY, n);
      if (!n) return;
      jmap_iter it = jmap_iter_init((jmap_node *)jaclrt_as_ptr(v));
      for (uint32_t i = 0; i < n && jaclrt_is_nil(e->err); i++) {
        jmap_iter_result r = jmap_next_leaf(&it);
        if (r.done) break;
        JaclVal k = r.item->slots[0];
        JaclVal val = *(JaclVal *)((char *)r.item->slots + sizeof(JaclVal) * r.item->key_stride);
        uint64_t ent = off + (uint64_t)i * JV_ENTRY;
        jv_put(e, k, ent, depth + 3);
        jv_put(e, val, ent + JV_HEAD, depth + 3);
      }
      return;
    }
    default: break;
  }
  e->err = jv_err("value: cannot cross a vat boundary: ", v);
}

/* The size of `v`'s encoding, or 0 with `*err` set if it cannot cross. */
static uint64_t jv_size(JaclVal v, JaclVal *err) {
  JvEnc e = {0, JV_HEAD, JACL_NIL};
  jv_put(&e, v, 0, 0);
  *err = e.err;
  return jaclrt_is_nil(e.err) ? e.len : 0;
}

/* Encodes `v` into `buf`, which holds `jv_size(v)` zeroed bytes. */
static void jv_write(JaclVal v, uint8_t *buf) {
  JvEnc e = {buf, JV_HEAD, JACL_NIL};
  jv_put(&e, v, 0, 0);
}

/* Encodes `v` into `buf` of `cap` bytes: its length, or 0 with `*err` set (it cannot cross,
 * or it takes more than `cap`). */
static uint64_t jv_encode_into(JaclVal v, uint8_t *buf, uint64_t cap, JaclVal *err) {
  uint64_t n = jv_size(v, err);
  if (!n) return 0;
  if (n > cap) {
    *err = jacl_error_msg_i("value: too large for its frame, bytes: ", (int32_t)(n > 0x7FFFFFFF ? 0x7FFFFFFF : n));
    return 0;
  }
  memset(buf, 0, n);
  jv_write(v, buf);
  return n;
}

/* ---- decoding (verified frames only) ---- */

static uint64_t jv_le(const uint8_t *p, int n) {
  uint64_t x = 0;
  for (int k = 0; k < n; k++) x |= (uint64_t)p[k] << (8 * k);
  return x;
}

static JaclVal jv_get(const uint8_t *f, uint64_t at);

/* The list<Value> slot at `at` as a vector. */
static JaclVal jv_get_vec(const uint8_t *f, uint64_t at) {
  uint64_t off = jv_le(f + at, 4), n = jv_le(f + at + 4, 4);
  JaclVal keep[2] = {jacl_vec_empty(), JACL_NIL};   /* on the data stack: collections allocate */
  for (uint64_t i = 0; i < n; i++) {
    keep[1] = jv_get(f, off + i * JV_HEAD);
    keep[0] = jacl_vec_push(keep[0], keep[1]);
  }
  return keep[0];
}

/* The value whose head is at `at` in the verified frame `f`. */
static JaclVal jv_get(const uint8_t *f, uint64_t at) {
  const uint8_t *u = f + at + 8;
  switch (f[at]) {
    case jv_Value_Nil: return JACL_NIL;
    case jv_Value_Bool: return jaclrt_bool(u[0] != 0);
    case jv_Value_I32: return jaclrt_i32((int32_t)(uint32_t)jv_le(u, 4));
    case jv_Value_I64: return jacl_int_result((int64_t)jv_le(u, 8));
    case jv_Value_U32: return jacl_int_result((int64_t)jv_le(u, 4));
    case jv_Value_U64: return jacl_big_from_u64(jv_le(u, 8));
    case jv_Value_F32: return JACL_TAG_F32 | jv_le(u, 4);
    case jv_Value_F64: return jacl_wide_new(0x10, (int64_t)jv_le(u, 8));
    case jv_Value_Text:
      return jacl_str_new((const char *)f + jv_le(u, 4), (uint32_t)jv_le(u + 4, 4));
    case jv_Value_Bytes: {
      int32_t n = (int32_t)jv_le(u + 4, 4);
      JaclVal b = fb_alloc_nd(1, 1, &n);
      if (n) memcpy(fb_data(b), f + jv_le(u, 4), (size_t)n);
      return b;
    }
    case jv_Value_Vec: return jv_get_vec(f, at + 8);
    case jv_Value_Map: {
      uint64_t off = jv_le(u, 4), n = jv_le(u + 4, 4);
      JaclVal keep[3] = {jacl_map_empty(), JACL_NIL, JACL_NIL};
      for (uint64_t i = 0; i < n; i++) {
        keep[1] = jv_get(f, off + i * JV_ENTRY);
        keep[2] = jv_get(f, off + i * JV_ENTRY + JV_HEAD);
        keep[0] = jacl_map_set(keep[0], keep[1], keep[2]);
      }
      return keep[0];
    }
    case jv_Value_Error: {
      /* The payload rides in a one-element list (value.usc); any other length is not an error
       * JACL wrote, and reads as one saying so. */
      if (jv_le(u + 4, 4) != 1) return jv_err("value: an error must carry one payload", JACL_NIL);
      return jacl_error_new(jv_get(f, jv_le(u, 4)));
    }
  }
  return jv_err("value: unknown variant", JACL_NIL);   /* unreachable once verified */
}

/* The value `frame` encodes, verifying it first: an error value if it is not a value. */
static JaclVal jv_decode(const uint8_t *frame, uint64_t len) {
  if (!jv_verify(frame, len)) return jv_err("value: malformed", JACL_NIL);
  return jv_get(frame, 0);
}

/* The value in a CHAN_VALUES read end's tail: a message the agreement verified, copied into this
 * vat's memory (chan_unir_next). */
static JaclVal chan_value(JaclChan *c) { return jv_get(c->tail, 0); }
